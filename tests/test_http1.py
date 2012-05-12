# -*- coding: utf-8 -*-

import util
import requests

def test_simple():

    def client():
        return requests.get('http://localhost:8000/')
    
    env, res = util.run_client(client)
    assert("/" == env["PATH_INFO"])

def test_encode():

    def client():
        return requests.get('http://localhost:8000/あいう')
    
    env, res = util.run_client(client)
    assert("/あいう" == env["PATH_INFO"])


def test_query():

    def client():
        return requests.get('http://localhost:8000/?a=1234&bbbb=ccc')
    
    env, res = util.run_client(client)
    assert("a=1234&bbbb=ccc" == env["QUERY_STRING"])

# def test_chunk_response():

    # def client():
        # return requests.get('http://localhost:8000/')
    
    # env, res = util.run_client(client)
    # print(res.headers)

    # assert("a=1234&bbbb=ccc" == env["QUERY_STRING"])

