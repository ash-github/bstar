#pragma once
#include <iostream>
#include <cassert>
#include <chrono>

enum State
{
	STATE_PRIMARY,          //  Primary, waiting for peer to connect
	STATE_BACKUP,           //  Backup, waiting for peer to connect
	STATE_ACTIVE,           //  Active - accepting connections
	STATE_PASSIVE           //  Passive - not accepting connections
};

//  Events, which start with the states our peer can be in
enum Event
{
	PEER_PRIMARY,           //  HA peer is pending primary
	PEER_BACKUP,            //  HA peer is pending backup
	PEER_ACTIVE,            //  HA peer is active
	PEER_PASSIVE,           //  HA peer is passive
	CLIENT_REQUEST          //  Client makes request
};

const std::int64_t HEARTBEAT = 1000;          //  In msecs
											  //  The heart of the Binary Star design is its finite-state machine (FSM).
											  //  The FSM runs one event at a time. We apply an event to the current state,
											  //  which checks if the event is accepted, and if so, sets a new state:

int64_t get_current_millis()
{
	auto n = std::chrono::system_clock::now();
	auto m = n.time_since_epoch();
	auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(m).count();
	//auto const msecs = diff % 1000;
	return diff;
}

struct fsm_t
{
	//  Our finite state machine
	State state;        //  Current state
	Event event;        //  Current event
	int64_t peerExpiry;    //  When peer is considered 'dead'

							   //  We send state information this often
							   //  If peer doesn't respond in two heartbeats, it is 'dead'
public:

	bool state_machine()
	{
		bool exception = false;

		//  These are the PRIMARY and BACKUP states; we're waiting to become
		//  ACTIVE or PASSIVE depending on events we get from our peer:
		if (state == State::STATE_PRIMARY)
		{
			if (event == Event::PEER_BACKUP)
			{
				std::cout << "I: connected to backup (passive), ready active\n";
				state = State::STATE_ACTIVE;
				std::cout << "I: current state STATE_ACTIVE\n";
			}
			else if (event == Event::PEER_ACTIVE)
			{
				std::cout << "I: connected to backup (active), ready passive\n";
				state = State::STATE_PASSIVE;
				std::cout << "I: current state STATE_PASSIVE\n";
			}
			//  Accept client connections
		}
		else if (state == State::STATE_BACKUP)
		{
			if (event == Event::PEER_ACTIVE)
			{
				std::cout << "I: connected to primary (active), ready passive\n";
				state = State::STATE_PASSIVE;
				std::cout << "I: current state STATE_PASSIVE\n";
			}
			else if (event == Event::CLIENT_REQUEST)//  Reject client connections when acting as backup
			{
				std::cout << "I: exception, acting as backup\n";
				exception = true;
			}
		}
		else if (state == State::STATE_ACTIVE) //These are the ACTIVE and PASSIVE states:
		{
			if (event == Event::PEER_ACTIVE)
			{
				//  Two actives would mean split-brain
				std::cout << "E: fatal error - dual actives, aborting\n";
				exception = true;
			}
		}
		else if (state == State::STATE_PASSIVE) //  Server is passive //  CLIENT_REQUEST events can trigger failover if peer looks dead		
		{
			if (event == Event::PEER_PRIMARY)
			{
				//  Peer is restarting - become active, peer will go passive
				std::cout << "I: primary (passive) is restarting, ready active\n";
				state = State::STATE_ACTIVE;
				std::cout << "I: current state STATE_ACTIVE\n";
			}
			else if (event == Event::PEER_BACKUP)
			{
				//  Peer is restarting - become active, peer will go passive
				std::cout << "I: backup (passive) is restarting, ready active\n";
				state = State::STATE_ACTIVE;
				std::cout << "I: current state STATE_ACTIVE\n";
			}
			else if (event == Event::PEER_PASSIVE)
			{
				//  Two passives would mean cluster would be non-responsive
				std::cout << "E: fatal error - dual passives, aborting\n";
				exception = true;
			}
			else if (event == Event::CLIENT_REQUEST)
			{
				//  Peer becomes active if timeout has passed
				//  It's the client request that triggers the failover
				assert(peerExpiry > 0);
				if (get_current_millis() >= peerExpiry)
				{
					//  If peer is dead, switch to the active state
					std::cout << "I: failover successful, ready active\n";
					state = State::STATE_ACTIVE;
					std::cout << "I: current state STATE_ACTIVE\n";
				}
				else
				{
					//  If peer is alive, reject connections
					exception = true;
					std::cout << "I: reject connections\n";
				}					
			}
		}
		return exception;
	}

};