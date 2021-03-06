from cgi import parse_qs, escape
import xml.etree.ElementTree as ET

from collections import OrderedDict

from BaseService import BaseService

import redis

import simplejson as json

import http.client

from lib.Exceptions.OS_BaseException import OS_BaseException
from lib.Exceptions.OS_CommonExceptions import *

class OS_WebProfileMgr(BaseService):
    def test_required_params(self, input, params):
        for param in params:
            if param not in input:
                raise OS_MissingParam(param)

        return True

    def test_profile_ownership(self, session_key, profileid):
        send_data = {'session_key': session_key, 'profileid': profileid, 'mode': 'test_session_profileid'}
        params = json.dumps(send_data)
        
        headers = {
            "Content-type": "application/json",
            "Accept": "text/plain",
            "APIKey": self.BACKEND_PRIVATE_APIKEY
        }
        conn = http.client.HTTPConnection(self.LOGIN_SERVER)

        conn.request("POST", self.LOGIN_SCRIPT, params, headers)
        response = json.loads(conn.getresponse().read())
        return response['valid']

    def test_user_session(self, session_key, userid):
        send_data = {'session_key': session_key, 'userid': userid, 'mode': 'test_session'}
        send_data = json.dumps(send_data)
        
        headers = {
            "Content-type": "application/json",
            "Accept": "text/plain",
            "APIKey": self.BACKEND_PRIVATE_APIKEY
        }
        conn = http.client.HTTPConnection(self.LOGIN_SERVER)

        conn.request("POST", self.LOGIN_SCRIPT, send_data, headers)
        response = json.loads(conn.getresponse().read())
        return response

    def handle_update_profile(self, data):

        #passthrough_params = ["session_key"]
        passthrough_profile_params = ["uniquenick", "nick", "id", "firstname", "lastname", "icquin", "sex", "homepage", "zipcode"]

        profile = {}
        for key in passthrough_profile_params:
            if key in data["profile"]:
                profile[key] = data["profile"][key]
        send_data = {'mode': 'update_profile', 'profile': profile, 'session_key': data['session_key']}
        send_data = json.dumps(send_data)
        
        
        headers = {
            "Content-type": "application/json",
            "Accept": "text/plain",
            "APIKey": self.BACKEND_PRIVATE_APIKEY
        }
        conn = http.client.HTTPConnection(self.PROFILE_MGR_SERVER)

        conn.request("POST", self.PROFILE_MGR_SCRIPT, send_data, headers)
        response = json.loads(conn.getresponse().read())


        return response
    def handle_get_profiles(self, data):

        if "userid" not in data:
            raise OS_MissingParam("userid")
        request_data = {'session_key': data['session_key'], 'userid': data['userid'], 'mode': 'get_profiles'}

        params = json.dumps(request_data)
                
        headers = {
            "Content-type": "application/json",
            "Accept": "text/plain",
            "APIKey": self.BACKEND_PRIVATE_APIKEY
        }
        conn = http.client.HTTPConnection(self.PROFILE_MGR_SERVER)

        conn.request("POST", self.PROFILE_MGR_SCRIPT, params, headers)
        response = json.loads(conn.getresponse().read())

        return response

    def handle_create_profile(self, data):
        response = {}

        passthrough_profile_params = ["nick", "uniquenick", "namespaceid", "nick", "id", "firstname", "lastname", "icquin", "sex", "homepage"]

        profile = {}
        for key in passthrough_profile_params:
            if key in data["profile"]:
                profile[key] = data["profile"][key]

        request_data = {'session_key': data['session_key'], 'userid': data['userid'], 'mode': 'create_profile', 'profile': profile}

        params = json.dumps(request_data)        
        
        headers = {
            "Content-type": "application/json",
            "Accept": "text/plain",
            "APIKey": self.BACKEND_PRIVATE_APIKEY
        }
        conn = http.client.HTTPConnection(self.PROFILE_MGR_SERVER)

        conn.request("POST", self.PROFILE_MGR_SCRIPT, params, headers)
        response = json.loads(conn.getresponse().read())

        return response

    def handle_delete_profile(self, data):

        request_data = {'session_key': data['session_key'], 'userid': data['userid'], 'mode': 'delete_profile', 'profileid': data["profile"]["id"]}

        params = json.dumps(request_data)
        
        headers = {
            "Content-type": "application/json",
            "Accept": "text/plain",
            "APIKey": self.BACKEND_PRIVATE_APIKEY
        }
        conn = http.client.HTTPConnection(self.PROFILE_MGR_SERVER)

        conn.request("POST", self.PROFILE_MGR_SCRIPT, params, headers)
        response = json.loads(conn.getresponse().read())
        return response

    def process_request(self, data):

        if "mode" not in data:
            raise OS_MissingParam("mode")

        has_ownership = False

        profile_ownership_modes = ["update_profile", "delete_profile"]
        user_ownership_modes = ["create_profile", "get_profiles"]

        session_data = self.test_user_session(data["session_key"], data["userid"])

        if data["mode"] in profile_ownership_modes:
            if "session_key" in data and "profile" in data:
                has_ownership = self.test_profile_ownership(data["session_key"], data["profile"]["id"]) or session_data['admin']
        elif data["mode"] in user_ownership_modes:
            if "session_key" in data and "userid" in data:
                has_ownership = session_data['valid'] or session_data['admin']

        if not has_ownership:
            raise OS_Auth_InvalidCredentials()

        mode_table = {
            "update_profile": self.handle_update_profile,
            "get_profiles": self.handle_get_profiles,
            "create_profile":  self.handle_create_profile,
            "delete_profile": self.handle_delete_profile
        }

        req_type = data["mode"]
        if req_type in mode_table:
            response = mode_table[req_type](data)
        else:
            raise OS_InvalidMode()
        return response
        
        
    def run(self, env, start_response):
        # the environment variable CONTENT_LENGTH may be empty or missing
        try:
            request_body_size = int(env.get('CONTENT_LENGTH', 0))
        except (ValueError):
            request_body_size = 0

        # When the method is POST the variable will be sent
        # in the HTTP request body which is passed by the WSGI server
        # in the file like wsgi.input environment variable.
        request_body = json.loads(env['wsgi.input'].read(request_body_size))
        # d = parse_qs(request_body)

        try:
            response = self.process_request(request_body)
        except OS_BaseException as e:
            response = e.to_dict()
        except Exception as error:
            response = {"error": repr(error)}

        #if 'error' in response:
        #   start_response('400 BAD REQUEST', [('Content-Type','application/json')])
        # else:
        start_response('200 OK', [('Content-Type','application/json')])
        return response