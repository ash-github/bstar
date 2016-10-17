#include <iostream>
#include <atomic>
#include <string>
#include "fsm.hpp"
#include <rest_rpc/rpc.hpp>

namespace client
{
	TIMAX_DEFINE_PROTOCOL(heartbeat, int(int));
	TIMAX_DEFINE_PROTOCOL(echo, std::string(const std::string&));
}

using server_t = timax::rpc::server<timax::rpc::msgpack_codec>;
using async_client = timax::rpc::async_client<timax::rpc::msgpack_codec>;

bool init_role(fsm_t& fsm, std::unique_ptr<server_t>& sp, boost::asio::ip::tcp::endpoint& sub_endpoint, boost::asio::ip::tcp::endpoint& pub_endpoint, const std::string& input, const std::string& peer_ip)
{
	std::atomic<bool> b = true;
	if (input == "-p")
	{
		std::cout << "I: Primary active, waiting for backup (passive)\n";
		sp = std::make_unique<server_t>(5001, std::thread::hardware_concurrency());
		pub_endpoint = timax::rpc::get_tcp_endpoint(peer_ip, 5002); //remote server
		sub_endpoint = timax::rpc::get_tcp_endpoint("127.0.0.1", 5001); //sub self
		fsm.state = State::STATE_PRIMARY;
		std::cout << "I: current state STATE_PRIMARY\n";
	}
	else if (input == "-b")
	{
		std::cout << "I: Backup passive, waiting for primary (active)\n";
		sp = std::make_unique<server_t>(5002, std::thread::hardware_concurrency());
		pub_endpoint = timax::rpc::get_tcp_endpoint(peer_ip, 5001); //remote server
		sub_endpoint = timax::rpc::get_tcp_endpoint("127.0.0.1", 5002); //sub self
		fsm.state = State::STATE_BACKUP;
		std::cout << "I: current state STATE_BACKUP\n";
	}
	else if (input == "-c")
	{
		async_client client;
		auto endpoint = timax::rpc::get_tcp_endpoint("127.0.0.1", 5001);
		
		while (b)
		{
			client.call(endpoint, client::echo, "test").on_ok([](const std::string& str)
			{
				std::cout << str << std::endl;
			}).on_error([&b](auto const& error)
			{
				b = false;
				std::cout << error.get_error_message() << std::endl;
			});

			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
	}
	else
	{
		std::cout << "Usage: bstar { -p | -b }\n";
		b = false;
	}

	return b;
}

void send_heartbeat(fsm_t& fsm, async_client& pub_client, boost::asio::ip::tcp::endpoint& pub_endpoint, std::atomic<int64_t>& send_state_at)
{
	std::thread thd([&pub_client, &pub_endpoint, &fsm, &send_state_at]
	{
		while (true)
		{
			try
			{
				send_state_at = get_current_millis() + HEARTBEAT;
				auto task = pub_client.call(pub_endpoint, client::heartbeat, fsm.state);
				task.wait();
			}
			catch (timax::rpc::exception const& e)
			{
				std::cout << e.get_error_message() << std::endl;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		}
	});

	thd.detach();
}

void handle_recv_msg(fsm_t& fsm, async_client& sub_client, boost::asio::ip::tcp::endpoint& sub_endpoint, std::atomic<int64_t>& send_state_at, bool& b)
{
	sub_client.sub(sub_endpoint, client::heartbeat, [&fsm, &send_state_at](auto r)
	{
		int timeLeft = (int)((send_state_at - get_current_millis()));
		if (timeLeft < 0)
			timeLeft = 0;

		fsm.event = (Event)r;

		if (fsm.state_machine())
			throw timax::rpc::exception();          //  Error, so exit

		fsm.peerExpiry = get_current_millis() + 2 * HEARTBEAT;
	},
		[&b](auto const& error)
	{
		b = false;
		std::cout << error.get_error_message() << std::endl;
	});
}

int main(int argc, char* argv[])
{
	if (argc < 2)
		return -1;

	std::string cmd = argv[1];

	//std::string cmd;
	//std::cin >> cmd;
	std::string peer_ip = argc==3? argv[2]:"127.0.0.1";

	timax::log::get().init("rest_rpc_server.lg");
	std::unique_ptr<server_t> sp = nullptr;

	timax::log::get().init("rest_rpc_client.lg");
	async_client sub_client;
	boost::asio::ip::tcp::endpoint sub_endpoint;

	async_client pub_client;
	boost::asio::ip::tcp::endpoint pub_endpoint;

	//init role and the state
	fsm_t fsm;
	bool b = init_role(fsm, sp, sub_endpoint, pub_endpoint, cmd, peer_ip);
	if (!b)
		return -1;

	//register busniess logic
	sp->register_handler("heartbeat", [](int state) { return state; }, [&sp](auto conn, int state) { sp->pub("heartbeat", state); });
	sp->register_handler("echo", [&fsm](const std::string& str)
	{ 
		fsm.event = Event::CLIENT_REQUEST;
		if (fsm.state_machine())
			throw timax::rpc::exception(timax::rpc::error_code::FAIL, "reject");

		return str; 
	});
	sp->start();

	//periodically sending heartbeat, message is the current state
	std::atomic<int64_t> send_state_at = get_current_millis() + HEARTBEAT;
	send_heartbeat(fsm, pub_client, pub_endpoint, send_state_at);
	
	//handle recieved message and change fsm.
	handle_recv_msg(fsm, sub_client, sub_endpoint, send_state_at, b);

	if (!b)
		return -1;

	std::cin >> cmd;
}