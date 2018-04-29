#include "FESLDriver.h"
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>

#include "FESLServer.h"
#include "FESLPeer.h"


namespace FESL {
	Driver::Driver(INetServer *server, const char *host, uint16_t port, PublicInfo public_info, bool use_ssl, const char *x509_path, const char *rsa_priv_path, SSLNetIOIFace::ESSL_Type ssl_version) : INetDriver(server) {

		//setup config vars
		m_server_info.domainPartition = public_info.domainPartition;
		m_server_info.subDomain = public_info.subDomain;
		m_server_info.messagingHostname = public_info.messagingHostname;
		m_server_info.messagingPort = public_info.messagingPort;
		m_server_info.theaterHostname = public_info.theaterHostname;
		m_server_info.theaterPort = public_info.theaterPort;

		mp_socket_interface = new SSLNetIOIFace::SSLNetIOInterface(ssl_version, "", "");

		mp_socket = mp_socket_interface->BindTCP(OS::Address(0, port));
		mp_mutex = OS::CreateMutex();

		m_encrypted_login_info_key = NULL;

	}
	Driver::~Driver() {
		std::vector<Peer *>::iterator it = m_connections.begin();
		while (it != m_connections.end()) {
			Peer *peer = *it;
			delete peer;
			it++;
		}

		delete mp_mutex;
		delete mp_thread;

		if (m_encrypted_login_info_key) {
			RSA_free(m_encrypted_login_info_key);
		}
	}
	void *Driver::TaskThread(OS::CThread *thread) {
		Driver *driver = (Driver *)thread->getParams();
		for (;;) {
			driver->mp_mutex->lock();
			std::vector<Peer *>::iterator it = driver->m_connections.begin();
			while (it != driver->m_connections.end()) {
				Peer *peer = *it;
				if (peer->ShouldDelete() && std::find(driver->m_peers_to_delete.begin(), driver->m_peers_to_delete.end(), peer) == driver->m_peers_to_delete.end()) {
					//marked for delection, dec reference and delete when zero
					it = driver->m_connections.erase(it);
					peer->DecRef();

					driver->m_server->UnregisterSocket(peer);

					driver->m_stats_queue.push(peer->GetPeerStats());
					driver->m_peers_to_delete.push_back(peer);
					continue;
				}
				it++;
			}

			it = driver->m_peers_to_delete.begin();
			while (it != driver->m_peers_to_delete.end()) {
				FESL::Peer *p = *it;
				if (p->GetRefCount() == 0) {
					delete p;
					it = driver->m_peers_to_delete.erase(it);
					continue;
				}
				it++;
			}

			driver->TickConnections();
			driver->mp_mutex->unlock();
			OS::Sleep(DRIVER_THREAD_TIME);
		}
	}
	void Driver::think(bool listener_waiting) {
		if (listener_waiting) {
			std::vector<INetIOSocket *> sockets = getSSL_Socket_Interface()->TCPAccept(mp_socket);
			std::vector<INetIOSocket *>::iterator it = sockets.begin();
			while (it != sockets.end()) {
				INetIOSocket *sda = *it;
				if (sda == NULL) return;
				Peer *mp_peer = NULL;

				mp_peer = new Peer(this, sda);

				mp_mutex->lock();
				m_connections.push_back(mp_peer);
				m_server->RegisterSocket(mp_peer);
				mp_mutex->unlock();
				it++;
			}
		}
	}

	const std::vector<INetPeer *> Driver::getPeers(bool inc_ref) {
		mp_mutex->lock();
		std::vector<INetPeer *> peers;
		std::vector<Peer *>::iterator it = m_connections.begin();
		while (it != m_connections.end()) {
			INetPeer * p = (INetPeer *)*it;
			peers.push_back(p);
			if (inc_ref)
				p->IncRef();
			it++;
		}
		mp_mutex->unlock();
		return peers;
	}

	const std::vector<INetIOSocket *> Driver::getSockets() const {
		std::vector<INetIOSocket *> ret;
		std::vector<Peer *>::const_iterator it = m_connections.begin();
		while (it != m_connections.end()) {
			Peer *peer = *it;
			ret.push_back(peer->GetSocket());
			it++;
		}
		return ret;
	}

	void Driver::TickConnections() {
		std::vector<Peer *>::iterator it = m_connections.begin();
		while (it != m_connections.end()) {
			Peer *p = *it;
			p->think(false);
			it++;
		}
	}
	OS::MetricInstance Driver::GetMetrics() {
		OS::MetricInstance peer_metric;
		OS::MetricValue arr_value2, value, peers;

		mp_mutex->lock();

		std::vector<Peer *>::iterator it = m_connections.begin();
		while (it != m_connections.end()) {
			INetPeer * peer = (INetPeer *)*it;
			OS::Address address = peer->getAddress();
			value = peer->GetMetrics().value;

			value.key = address.ToString(false);

			peers.arr_value.values.push_back(std::pair<OS::MetricType, struct OS::_Value>(OS::MetricType_Array, value));
			it++;
		}

		while (!m_stats_queue.empty()) {
			PeerStats stats = m_stats_queue.front();
			m_stats_queue.pop();
			peers.arr_value.values.push_back(std::pair<OS::MetricType, struct OS::_Value>(OS::MetricType_Array, Peer::GetMetricItemFromStats(stats)));
		}

		peers.key = "peers";
		arr_value2.type = OS::MetricType_Array;
		peers.type = OS::MetricType_Array;
		arr_value2.arr_value.values.push_back(std::pair<OS::MetricType, struct OS::_Value>(OS::MetricType_Array, peers));


		peer_metric.key = mp_socket->address.ToString(false);
		arr_value2.key = peer_metric.key;
		peer_metric.value = arr_value2;

		mp_mutex->unlock();
		return peer_metric;
	}
	std::string Driver::decryptString(std::string input) {
		std::string ret;
		uint8_t *b64_out;
		int out_len = 0;

		size_t pos = input.find("%3d");
		if(pos != std::string::npos)
			input.replace(pos, input.length(), "=");

		int mem_len = RSA_size(m_encrypted_login_info_key);
		unsigned char *buf = (unsigned char *)malloc(mem_len);

		OS::Base64StrToBin(input.c_str(), &b64_out, out_len);
		RSA_private_decrypt(out_len, b64_out,buf, m_encrypted_login_info_key, RSA_PKCS1_PADDING);
		ret = std::string((char *)buf);
		if (b64_out)
			free((void *)b64_out);

		if (buf) {
			free((void *)buf);
		}
		return ret;
	}
	std::string Driver::encryptString(std::string input) {
		std::string ret;
		int mem_len = RSA_size(m_encrypted_login_info_key);
		unsigned char *buf = (unsigned char *)malloc(mem_len);
		RSA_public_encrypt(input.length()+1, (const unsigned char *)input.c_str(),buf, m_encrypted_login_info_key, RSA_PKCS1_PADDING);
		const char *b64_str = OS::BinToBase64Str(buf, mem_len);
		ret = std::string(b64_str);
		free((void *)b64_str);
		free((void *)buf);
		return ret;
	}

	INetIOSocket *Driver::getListenerSocket() const {
		return mp_socket;
	}

	SSLNetIOIFace::SSLNetIOInterface *Driver::getSSL_Socket_Interface() {
		return mp_socket_interface;
	}
}