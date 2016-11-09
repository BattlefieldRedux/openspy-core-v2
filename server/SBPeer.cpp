#include "SBPeer.h"
#include "SBDriver.h"
#include "../legacy/buffreader.h"
#include "../legacy/buffwriter.h"
#include "../legacy/enctypex_decoder.h"
#include "../socketlib/socketlib.h"
#define CRYPTCHAL_LEN 10
#define SERVCHAL_LEN 25
namespace SB {
	Peer::Peer(Driver *driver, struct sockaddr_in *address_info, int sd) {
		m_sd = sd;
		m_sent_crypt_header = false;
		m_address_info = *address_info;
	}
	Peer::~Peer() {
	}
	void Peer::handle_packet(char *data, int len) {
		uint8_t *buffer = (uint8_t *)data;
		uint32_t pos = len;
		
		uint8_t request_type;

		BufferReadShort(&buffer, &pos);
		request_type = BufferReadByte(&buffer, &pos);

		switch (request_type) {
			case SERVER_LIST_REQUEST:
				ProcessListRequset(buffer, pos);
				break;
		}

	}
	sServerListReq Peer::ParseListRequest(uint8_t *buffer, int remain) {
		uint8_t listversion, encodingversion;
		uint32_t fromgamever; //game version
		uint32_t srcip = 0;
		uint32_t maxServers = 0;
		uint32_t options;
		uint32_t buf_remain = remain;

		const char *from_gamename, *for_gamename, *filter, *field_list;

		sServerListReq req;

		listversion = BufferReadByte(&buffer, &buf_remain);
		encodingversion = BufferReadByte(&buffer, &buf_remain);

		fromgamever = BufferReadInt(&buffer, &buf_remain);

		req.protocol_version = listversion;
		req.encoding_version = encodingversion;

		from_gamename = (const char *)BufferReadNTS(&buffer, &buf_remain);
		for_gamename = (const char *)BufferReadNTS(&buffer, &buf_remain);

		if (from_gamename) {
			req.mp_from_game = OS::GetGameByName(from_gamename);
			free((void *)from_gamename);
		}
		else
			req.mp_from_game = NULL;

		if (from_gamename) {
			req.mp_for_game = OS::GetGameByName(for_gamename);
			free((void *)for_gamename);
		}
		else
			req.mp_for_game = NULL;

		

		BufferReadData(&buffer, &buf_remain, (uint8_t*)&m_challenge, LIST_CHALLENGE_LEN);

		req.filter = (const char *)BufferReadNTS(&buffer, &buf_remain);
		field_list = (const char *)BufferReadNTS(&buffer, &buf_remain);

		if (field_list) {
			req.field_list = KeyStringToMap(field_list);
			free((void *)field_list);
		}
		if (!req.field_list.size())
			req.field_list = KeyStringToMap("\\hostname\\numplayers\\maxplayers\\password\\region");

		options = BufferReadIntRE(&buffer, &buf_remain);

		req.send_groups = options & SEND_GROUPS;
		req.send_push_keys = options & PUSH_UPDATES; //requesting updates, 
		req.no_server_list = options & NO_SERVER_LIST;

		if (options & ALTERNATE_SOURCE_IP) {
			req.source_ip = BufferReadInt(&buffer, &buf_remain);
		}
		if (options & LIMIT_RESULT_COUNT) {
			req.max_results = BufferReadInt(&buffer, &buf_remain);
		}
		

		return req;

	}
	void Peer::SendListQueryResp(struct MM::ServerListQuery servers, sServerListReq *list_req) {
		/*
		TODO: make support split packets
		*/
		std::vector<MM::Server *>::iterator it = servers.list.begin();
		uint8_t buff[MAX_OUTGOING_REQUEST_SIZE * 2];
		uint8_t *p = (uint8_t *)&buff;
		uint32_t len = 0;

		BufferWriteInt(&p, &len, m_address_info.sin_addr.S_un.S_addr);
		BufferWriteShortRE(&p, &len, list_req->mp_from_game->queryport);

		BufferWriteShort(&p, &len, list_req->field_list.size());

		std::vector<std::string>::iterator field_it = list_req->field_list.begin();
		while (field_it != list_req->field_list.end()) {
			std::string str = *field_it;
			BufferWriteNTS(&p, &len, (uint8_t *)str.c_str());
			BufferWriteByte(&p, &len, 0); //on gamespys server there is a bug where a 2nd null byte is put after each word here so do it too for compatibility
			field_it++;
		}

		while (it != servers.list.end()) {
			uint8_t flags = HAS_KEYS_FLAG;
			MM::Server *server = *it;

			if (server->wan_address.port != server->game->queryport) {
				flags |= NONSTANDARD_PORT_FLAG;
			}
			BufferWriteByte(&p, &len, flags); //flags
			BufferWriteInt(&p, &len, Socket::htonl(server->wan_address.ip)); //ip

			if (flags & NONSTANDARD_PORT_FLAG) {
				BufferWriteShort(&p, &len, Socket::htons(server->wan_address.port));
			}
			
			if (flags & PRIVATE_IP_FLAG) {
				BufferWriteInt(&p, &len, Socket::htonl(server->lan_address.ip));
			}
			if (flags & NONSTANDARD_PRIVATE_PORT_FLAG) {
				BufferWriteShort(&p, &len, Socket::htons(server->lan_address.port));
			}
			

			std::vector<std::string>::iterator tok_it = list_req->field_list.begin();
			while (tok_it != list_req->field_list.end()) {
				BufferWriteByte(&p, &len, 0xFF);
				if (server->kvFields.find((*tok_it)) != server->kvFields.end()) {
					std::string value = server->kvFields[*tok_it].c_str();
					BufferWriteNTS(&p, &len, (uint8_t*)value.c_str());
				}
				else {
					BufferWriteByte(&p, &len, 0);
				}
				tok_it++;
			}
			it++;
		}

		//terminator
		BufferWriteByte((uint8_t **)&p, (uint32_t *)&len, 0x00);
		BufferWriteInt((uint8_t **)&p, (uint32_t *)&len, -1);

		uint32_t *packet_len = (uint32_t *)&buff;
		*packet_len = reverse_endian16(len);

		SendPacket((uint8_t *)&buff, len);
	}
	void Peer::SendPacket(uint8_t *buff, int len) {
		uint8_t out_buff[MAX_OUTGOING_REQUEST_SIZE * 2];
		uint8_t *p = (uint8_t*)&out_buff;
		uint32_t out_len = 0;
		uint32_t header_len = 0;
		if (!m_sent_crypt_header) {
			setupCryptHeader(&p, (uint32_t *)&out_len);
 			header_len = out_len;
			m_sent_crypt_header = true;
		}
		BufferWriteData(&p, &out_len, buff, len);
		enctypex_func6e((unsigned char *)&encxkeyb, ((unsigned char *)&out_buff) + header_len, out_len - header_len);
		send(m_sd, (const char *)&out_buff, out_len, 0);
	}
	void Peer::ProcessListRequset(uint8_t *buffer, int remain) {

		uint8_t buff[MAX_OUTGOING_REQUEST_SIZE * 2];
		uint8_t *p;
		uint32_t len = 0;

		sServerListReq list_req = this->ParseListRequest(buffer, remain);

		mp_game = list_req.mp_from_game;

		printf("Got list req game: %s\n", list_req.mp_for_game->gamename);

		MM::ServerListQuery servers;

		if (list_req.send_groups) {
			servers = MM::GetGroups(&list_req);
		}
		else {
			servers = MM::GetServers(&list_req);
		}
		SendListQueryResp(servers, &list_req);

	}
	void Peer::send_ping() {

	}
	void Peer::think(bool waiting_packet) {
		char buf[MAX_OUTGOING_REQUEST_SIZE + 1];
		socklen_t slen = sizeof(struct sockaddr_in);
		int len;
		if (waiting_packet) {
			len = recv(m_sd, (char *)&buf, sizeof(buf), 0);
			this->handle_packet(buf, len);
		}
		else {

		}
	}
	void Peer::setupCryptHeader(uint8_t **dst, uint32_t *len) {
		//	memset(&options->cryptkey,0,sizeof(options->cryptkey));
		srand(time(NULL));
		uint32_t cryptlen = CRYPTCHAL_LEN;
		uint8_t cryptchal[CRYPTCHAL_LEN];
		uint32_t servchallen = SERVCHAL_LEN;
		uint8_t servchal[SERVCHAL_LEN];
		int headerLen = (servchallen + cryptlen) + (sizeof(uint8_t) * 2);
		uint16_t *backendflags = (uint16_t *)(&cryptchal);
		for (uint32_t i = 0; i<cryptlen; i++) {
			cryptchal[i] = (uint8_t)rand();
		}
		*backendflags = 0;
		for (uint32_t i = 0; i<servchallen; i++) {
			servchal[i] = (uint8_t)rand();
		}
		BufferWriteByte(dst, len, cryptlen ^ 0xEC);
		BufferWriteData(dst, len, (uint8_t *)&cryptchal, cryptlen);
		BufferWriteByte(dst, len, servchallen ^ 0xEA);
		BufferWriteData(dst, len, (uint8_t *)&servchal, servchallen);
		enctypex_funcx((unsigned char *)&encxkeyb, (unsigned char *)mp_game->secretkey, (unsigned char *)m_challenge, (unsigned char *)&servchal, servchallen);
	}
}