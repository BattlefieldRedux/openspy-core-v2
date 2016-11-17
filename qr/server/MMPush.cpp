#include "MMPush.h"
#include <OS/socketlib/socketlib.h>

//#include <signal.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#define _WINSOCK2API_
#include <stdint.h>
#include <hiredis/adapters/libevent.h>
#undef _WINSOCK2API_
#include <OS/legacy/helpers.h>

namespace MM {
	redisContext *mp_redis_connection;
	const char *sb_mm_channel = "serverbrowsing.servers";

	const char *mp_pk_name = "QRID";
	redisAsyncContext *mp_redis_async_connection;
	void onRedisMessage(redisAsyncContext *c, void *reply, void *privdata) {
	    redisReply *r = (redisReply*)reply;
	    printf("Got reply: %p\n",r);
	    if (reply == NULL) return;

	    char gamename[OS_MAX_GAMENAME+1],from_ip[32], to_ip[32], data[MAX_BASE64_STR+1], type[32];

	    if (r->type == REDIS_REPLY_ARRAY) {
	    	if(r->elements == 3 && r->element[2]->type == REDIS_REPLY_STRING) {
	    			find_param(0, r->element[2]->str,(char *)&type, sizeof(type)-1);
	    			if(!strcmp(type,"send_msg")) {
		    			printf("Got raw: %s\n", r->element[2]->str);
		    			find_param(1, r->element[2]->str,(char *)&gamename, sizeof(gamename)-1);
		    			find_param(2, r->element[2]->str,(char *)&from_ip, sizeof(from_ip)-1);
		    			find_param(3, r->element[2]->str, (char *)&to_ip, sizeof(to_ip)-1);
		    			find_param(4, r->element[2]->str, (char *)&data, sizeof(data)-1);
		    			printf("Send msg to %s | %s | %s\n", gamename, from_ip, to_ip);
		    			printf("Data: %s\n",data);
	    			}
	    		
	    	}
	    }
	}
	void *setup_redis_async(OS::CThread *) {
		struct event_base *base = event_base_new();
		mp_redis_async_connection = redisAsyncConnect("127.0.0.1", 6379);
	    redisLibeventAttach(mp_redis_async_connection, base);
	    redisAsyncCommand(mp_redis_async_connection, onRedisMessage, NULL, "SUBSCRIBE %s",sb_mm_channel);
	    event_base_dispatch(base);
	    return NULL;
	}
	void Init() {
		struct timeval t;
		t.tv_usec = 0;
		t.tv_sec = 3;

		mp_redis_connection = redisConnectWithTimeout("127.0.0.1", 6379, t);

	    OS::CreateThread(setup_redis_async, NULL, true);

	}
	void PushServer(ServerInfo *server, bool publish) {
		int id = GetServerID();
		int groupid = 0;

		server->id = id;
		server->groupid = groupid;

		freeReplyObject(redisCommand(mp_redis_connection, "HSET %s:%d:%d: gameid %d",server->m_game.gamename,groupid,id,server->m_game.gameid));
		freeReplyObject(redisCommand(mp_redis_connection, "HSET %s:%d:%d: id %d",server->m_game.gamename,groupid,id,id));


		struct sockaddr_in addr;
		addr.sin_port = Socket::htons(server->m_address.port);
		addr.sin_addr.s_addr = Socket::htonl(server->m_address.ip);
		const char *ipinput = Socket::inet_ntoa(addr.sin_addr);
		



		freeReplyObject(redisCommand(mp_redis_connection, "HSET %s:%d:%d: gameid %d",server->m_game.gamename,groupid,id,server->m_game.gameid));
		freeReplyObject(redisCommand(mp_redis_connection, "HSET %s:%d:%d: wan_port %d",server->m_game.gamename,groupid,id,server->m_address.port));
		freeReplyObject(redisCommand(mp_redis_connection, "HSET %s:%d:%d: wan_ip \"%s\"",server->m_game.gamename,groupid,id,ipinput));

		freeReplyObject(redisCommand(mp_redis_connection, "INCR %s:%d:%d: num_beats",server->m_game.gamename,groupid,id));


		freeReplyObject(redisCommand(mp_redis_connection, "EXPIRE %s:%d:%d: 300",server->m_game.gamename,groupid,id));

		std::map<std::string, std::string>::iterator it = server->m_keys.begin();
		while(it != server->m_keys.end()) {
			std::pair<std::string, std::string> p = *it;
			freeReplyObject(redisCommand(mp_redis_connection, "HSET %s:%d:%d:custkeys %s \"%s\"",server->m_game.gamename,groupid,id,p.first.c_str(),p.second.c_str()));
			it++;
		}
		freeReplyObject(redisCommand(mp_redis_connection, "EXPIRE %s:%d:%d:custkeys 300",server->m_game.gamename,groupid,id));

		//std::map<std::string, std::vector<std::string> > m_player_keys;
		std::map<std::string, std::vector<std::string> >::iterator it2 = server->m_player_keys.begin();

		int i =0;
		std::pair<std::string, std::vector<std::string> > p;
		std::vector<std::string>::iterator it3;
		while(it2 != server->m_player_keys.end()) {
			p = *it2;
			it3 = p.second.begin();
			while(it3 != p.second.end()) {
				std::string s = *it3;
				freeReplyObject(redisCommand(mp_redis_connection, "HSET %s:%d:%d:custkeys_player_%d %s \"%s\"",server->m_game.gamename,groupid,id, i,p.first.c_str(),s.c_str()));
				i++;
				it3++;
			}
			freeReplyObject(redisCommand(mp_redis_connection, "EXPIRE %s:%d:%d:custkeys_player_%d 300",server->m_game.gamename,groupid,id, i,p.first.c_str()));
			i=0;
			it2++;
		}


		it2 = server->m_team_keys.begin();
		while(it2 != server->m_team_keys.end()) {
			p = *it2;
			it3 = p.second.begin();
			while(it3 != p.second.end()) {
				
				std::string s = *it3;
				freeReplyObject(redisCommand(mp_redis_connection, "HSET %s:%d:%d:custkeys_team_%d %s \"%s\"",server->m_game.gamename,groupid,id, i,p.first.c_str(),s.c_str()));
				i++;
				it3++;
			}
			freeReplyObject(redisCommand(mp_redis_connection, "EXPIRE %s:%d:%d:custkeys_team_%d 300",server->m_game.gamename,groupid,id, i));
			i=0;
			it2++;
		}

		if(publish)
			freeReplyObject(redisCommand(mp_redis_connection, "PUBLISH %s \\new\\%s:%d:%d:",sb_mm_channel,server->m_game.gamename,groupid,id));

	}
	void UpdateServer(ServerInfo *server) {
		//remove all keys and readd
		DeleteServer(server, false);
		PushServer(server, false);

		freeReplyObject(redisCommand(mp_redis_connection, "PUBLISH %s \\update\\%s:%d:%d:",sb_mm_channel,server->m_game.gamename,server->groupid,server->id));
	}
	void DeleteServer(ServerInfo *server, bool publish) {
		int groupid = server->groupid;
		int id = server->id;
		/*
		freeReplyObject(redisCommand(mp_redis_connection, "DEL %s:%d:%d:",server->m_game.gamename,server->groupid,server->id));
		freeReplyObject(redisCommand(mp_redis_connection, "DEL %s:%d:%d:custkeys",server->m_game.gamename,server->groupid,server->id));
		
		int i =0;
		int groupid = server->groupid;
		int id = server->id;

		std::map<std::string, std::vector<std::string> >::iterator it2 = server->m_player_keys.begin();

		std::pair<std::string, std::vector<std::string> > p;
		std::vector<std::string>::iterator it3;
		while(it2 != server->m_player_keys.end()) {
			p = *it2;
			it3 = p.second.begin();
			while(it3 != p.second.end()) { //XXX: will be duplicate deletes but better than writing stuff to delete indivually atm, rewrite later though
				std::string s = *it3;
				freeReplyObject(redisCommand(mp_redis_connection, "DEL %s:%d:%d:custkeys_player_%d",server->m_game.gamename,groupid,id, i));
				i++;
				it3++;
			}

			i=0;
			it2++;
		}


		it2 = server->m_team_keys.begin();
		while(it2 != server->m_team_keys.end()) {
			p = *it2;
			it3 = p.second.begin();
			while(it3 != p.second.end()) { //XXX: will be duplicate deletes but better than writing stuff to delete indivually atm, rewrite later though
				std::string s = *it3;
				freeReplyObject(redisCommand(mp_redis_connection, "DEL %s:%d:%d:custkeys_team_%d",server->m_game.gamename,groupid,id, i));
				i++;
				it3++;
			}

			i=0;
			it2++;
		}
		*/
		freeReplyObject(redisCommand(mp_redis_connection, "HSET %s:%d:%d: deleted 1",server->m_game.gamename,server->groupid,server->id));
		if(publish)
			freeReplyObject(redisCommand(mp_redis_connection, "PUBLISH %s \\del\\%s:%d:%d:",sb_mm_channel,server->m_game.gamename,groupid,id));
	}
	int GetServerID() {
		redisReply *reply;

		reply = (redisReply *)redisCommand(mp_redis_connection, "INCR %s", mp_pk_name);
		int ret = -1;
		if(reply && reply->type == REDIS_REPLY_INTEGER) {
			ret = reply->integer;
		}
		if(reply) {
			freeReplyObject(reply);
		}
		return ret;
	}
}