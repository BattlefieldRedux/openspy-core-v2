#include "SBPeer.h"
#include "SBServer.h"
#include "SBDriver.h"
SBServer::SBServer() : INetServer() {
	
}
SBServer::~SBServer() {
}
void SBServer::init() {
}
void SBServer::tick() {
	NetworkTick();
}
void SBServer::shutdown() {

}
