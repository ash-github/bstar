#include <iostream>
#include <atomic>
#include <string>
#include "fsm.hpp"
#include <rest_rpc/rpc.hpp>

void test_mili()
{
	fsm_t fsm;
	std::cout << fsm.get_current_millis() << std::endl;
	std::cout << fsm.get_current_millis() << std::endl;
	std::cout << fsm.get_current_millis() << std::endl;
}

void test_fsm(fsm_t fsm)
{
	fsm.peerExpiry = fsm.get_current_millis() + 2 * fsm.HEARTBEAT;

	fsm.event = Event::PEER_ACTIVE;
	bool b = fsm.state_machine();

	//fsm.event = Event::PEER_BACKUP;
	//b = fsm.state_machine();

	fsm.event = Event::CLIENT_REQUEST;
	b = fsm.state_machine();

	fsm.event = Event::PEER_BACKUP;
	b = fsm.state_machine();

	fsm.event = Event::PEER_PASSIVE;
	b = fsm.state_machine();

	fsm.event = Event::PEER_PRIMARY;
	b = fsm.state_machine();

	//  We now process events on our two input sockets, and process these
	//  events one at a time via our finite-state machine. Our "work" for
	//  a client request is simply to echo it back:

	//  Set timer for next outgoing state message
	int64_t sendStateAt = fsm.get_current_millis() + fsm.HEARTBEAT;
	while (true)
	{
		//PollItem[] items = 
		//{
		//	new PollItem(frontend, ZMQ.Poller.POLLIN),
		//	new PollItem(statesub, ZMQ.Poller.POLLIN),
		//};
		//int timeLeft = (int)((sendStateAt - fsm.get_current_millis()));
		//if (timeLeft < 0)
		//	timeLeft = 0;
		//int rc = ZMQ.poll(items, 2, timeLeft);
		//if (rc == -1)
		//	break;              //  Context has been shut down

		//if (items[0].isReadable()) 
		//{
		//	//  Have a client request
		//	ZMsg msg = ZMsg.recvMsg(frontend);
		//	fsm.event = Event.CLIENT_REQUEST;
		//	if (fsm.stateMachine() == false)
		//		//  Answer client by echoing request back
		//		msg.send(frontend);
		//	else
		//		msg.destroy();
		//}
		//if (items[1].isReadable()) 
		//{
		//	//  Have state from our peer, execute as event
		//	String message = statesub.recvStr();
		//	fsm.event = Event.values()[Integer.parseInt(message)];
		//	if (fsm.stateMachine())
		//		break;          //  Error, so exit
		//	fsm.peerExpiry = System.currentTimeMillis() + 2 * HEARTBEAT;
		//}
		////  If we timed out, send state to peer
		//if (System.currentTimeMillis() >= sendStateAt) 
		//{
		//	statepub.send(String.valueOf(fsm.state.ordinal()));
		//	sendStateAt = System.currentTimeMillis() + HEARTBEAT;
		//}
	}

	std::cout << "W: interrupted\n";

	//  Shutdown sockets and context
	//ctx.destroy();
}

namespace client
{
	TIMAX_DEFINE_PROTOCOL(controller, int(int));
	TIMAX_DEFINE_PROTOCOL(echo, std::string(const std::string&));
}

int main()
{
	std::string str;
	std::cin >> str;

	timax::log::get().init("rest_rpc_server.lg");
	using server_t = timax::rpc::server<timax::rpc::msgpack_codec>;
	std::unique_ptr<server_t> sp = nullptr;

	timax::log::get().init("rest_rpc_client.lg");
	using async_client = timax::rpc::async_client<timax::rpc::msgpack_codec>;
	async_client sub_client;
	boost::asio::ip::tcp::endpoint pub_endpoint;
	boost::asio::ip::tcp::endpoint sub_endpoint;

	//using async_client = timax::rpc::async_client<timax::rpc::msgpack_codec>;
	async_client pub_client;

	fsm_t fsm;
	if (str == "-p")
	{
		std::cout<<"I: Primary active, waiting for backup (passive)\n";
		sp = std::make_unique<server_t>(5001, std::thread::hardware_concurrency());
		pub_endpoint = timax::rpc::get_tcp_endpoint("127.0.0.1", 5002);
		sub_endpoint = timax::rpc::get_tcp_endpoint("127.0.0.1", 5001);
		fsm.state = State::STATE_PRIMARY;
		std::cout << "I: current state STATE_PRIMARY\n";
	}
	else if (str == "-b")
	{
		std::cout << "I: Backup passive, waiting for primary (active)\n";
		sp = std::make_unique<server_t>(5002, std::thread::hardware_concurrency());
		pub_endpoint = timax::rpc::get_tcp_endpoint("127.0.0.1", 5001);
		sub_endpoint = timax::rpc::get_tcp_endpoint("127.0.0.1", 5002);
		fsm.state = State::STATE_BACKUP;
		std::cout << "I: current state STATE_BACKUP\n";
	}
	else if (str == "-c")
	{
		async_client client;
		auto endpoint = timax::rpc::get_tcp_endpoint("127.0.0.1", 5001);
		std::atomic<bool> need_break = false;
		while (!need_break)
		{
			client.call(endpoint, client::echo, "test").on_ok([](const std::string& str)
			{
				std::cout << str << std::endl;
			}).on_error([&need_break](auto const& error)
			{
				need_break = true;
				std::cout << error.get_error_message() << std::endl;
			});

			std::this_thread::sleep_for(std::chrono::seconds(1));
		}	

		return -1;
	}
	else 
	{
		std::cout << "Usage: bstar { -p | -b }\n";
		return -1;
	}

	sp->register_handler("controller", [](int i) { return i; }, [&sp](auto conn, int r) { sp->pub("controller", r); });
	sp->register_handler("echo", [](const std::string& str) 
	{ 
		return str; 
	}, [&fsm](auto conn, auto s) {
		fsm.event = Event::CLIENT_REQUEST;
		if (fsm.state_machine())
			conn->close();
	});
	sp->start();
	std::atomic<int64_t> send_state_at = fsm.get_current_millis() + fsm.HEARTBEAT;
	std::thread thd([&pub_client, &pub_endpoint, &fsm,&send_state_at]
	{
		while (true)
		{
			try
			{
				send_state_at = fsm.get_current_millis() + fsm.HEARTBEAT;
				auto task = pub_client.call(pub_endpoint, client::controller, fsm.state);
				task.wait();
				//auto r = task.get();
			}
			catch (timax::rpc::exception const& e)
			{
				std::cout << e.get_error_message() << std::endl;
			}

			this_thread::sleep_for(std::chrono::milliseconds(1000));
		}
	});
	
	sub_client.sub(sub_endpoint, client::controller, [&fsm, &send_state_at](auto r)
	{
		int timeLeft = (int)((send_state_at - fsm.get_current_millis()));
		if (timeLeft < 0)
			timeLeft = 0;
		std::cout << "timeLeft: "<<timeLeft << std::endl;
		fsm.event = (Event)r;

		if (fsm.state_machine())
			throw timax::rpc::exception();          //  Error, so exit

		fsm.peerExpiry = fsm.get_current_millis() + 2 * fsm.HEARTBEAT;
	},
		[](auto const& error)
	{
		std::cout << error.get_error_message() << std::endl;
	});


	std::cin >> str;
}