#ifndef _SBPEER_H
#define _SBPEER_H
#include "../main.h"
#include "MMQuery.h"

//message types for outgoing requests
#define SERVER_LIST_REQUEST		0
#define SERVER_INFO_REQUEST		1
#define SEND_MESSAGE_REQUEST	2
#define KEEPALIVE_REPLY			3
#define MAPLOOP_REQUEST			4
#define PLAYERSEARCH_REQUEST	5

//message types for incoming requests
#define PUSH_KEYS_MESSAGE		1
#define PUSH_SERVER_MESSAGE		2
#define KEEPALIVE_MESSAGE		3
#define DELETE_SERVER_MESSAGE	4
#define MAPLOOP_MESSAGE			5
#define PLAYERSEARCH_MESSAGE	6

//server list update options
#define SEND_FIELDS_FOR_ALL		1
#define NO_SERVER_LIST			2
#define PUSH_UPDATES			4
#define SEND_GROUPS				32
#define NO_LIST_CACHE			64
#define LIMIT_RESULT_COUNT		128

#define ALTERNATE_SOURCE_IP 8

//Maximum length for the SQL filter string
#define MAX_FILTER_LEN 511

#define MAX_FIELD_LIST_LEN 256

#define MAX_OUTGOING_REQUEST_SIZE (MAX_FIELD_LIST_LEN + MAX_FILTER_LEN + 255)

#define LIST_CHALLENGE_LEN 8


//game server flags
#define UNSOLICITED_UDP_FLAG	1
#define PRIVATE_IP_FLAG			2
#define CONNECT_NEGOTIATE_FLAG	4
#define ICMP_IP_FLAG			8
#define NONSTANDARD_PORT_FLAG	16
#define NONSTANDARD_PRIVATE_PORT_FLAG	32
#define HAS_KEYS_FLAG					64
#define HAS_FULL_RULES_FLAG				128





namespace SB {
	struct sServerListReq {
		uint8_t protocol_version;
		uint8_t encoding_version;
		uint32_t game_version;
		
		char challenge[LIST_CHALLENGE_LEN];
		
		const char *filter;
		//const char *field_list;
		std::vector<std::string> field_list;
		
		uint32_t source_ip; //not entire sure what this is for atm
		uint32_t max_results;
		
		bool send_groups;
		bool send_wan_ip;
		bool push_updates;
		bool no_server_list;
		
		OS::GameData m_for_game;
		OS::GameData m_from_game;

		bool all_keys;

	};
	class Driver;
	class Server;

	struct sServerCache {
		char key[64];
		OS::Address wan_address;
		bool full_keys;
	};


	class Peer {
	public:
		Peer(Driver *driver, struct sockaddr_in *address_info, int sd);
		~Peer();
		
		virtual void think(bool packet_waiting) = 0;
		const struct sockaddr_in *getAddress() { return &m_address_info; }

		int GetSocket() { return m_sd; };



		bool ShouldDelete() { return m_delete_flag; };
		bool IsTimeout() { return m_timeout_flag; }

		int GetPing();

		bool serverMatchesLastReq(MM::Server *server, bool require_push_flag = true);

		virtual void informDeleteServers(MM::ServerListQuery servers) = 0;
		virtual void informNewServers(MM::ServerListQuery servers) = 0;
		virtual void informUpdateServers(MM::ServerListQuery servers) = 0;
	protected:
		void cacheServer(MM::Server *server);
		void DeleteServerFromCacheByIP(OS::Address address);
		sServerCache FindServerByIP(OS::Address address);


		int m_sd;
		OS::GameData m_game;
		Driver *mp_driver;

		struct sockaddr_in m_address_info;

		struct timeval m_last_recv, m_last_ping;

		bool m_delete_flag;
		bool m_timeout_flag;

		std::vector<sServerCache> m_visible_servers;

		sServerListReq m_last_list_req;
	private:




	};
}
#endif //_SAMPRAKPEER_H