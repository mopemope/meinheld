# -*- coding: utf-8 -*-

import util
import requests

def test_simple():

    def client():
        return requests.get('http://localhost:8000/')
    
    env, data = util.run_client(client)
    assert("/" == env["PATH_INFO"])

def test_encode():

    def client():
        return requests.get('http://localhost:8000/あいう')
    
    env, data = util.run_client(client)
    assert("/あいう" == env["PATH_INFO"])




