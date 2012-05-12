# -*- coding: utf-8 -*-

import util
import requests

def test_simple():

    def client():
        return requests.get("http://localhost:8000/")
    
    env, res = util.run_client(client)
    # print(res.content)
    assert(util.RESPONSE == res.content)
    assert("/" == env["PATH_INFO"])
    assert(None == env.get("QUERY_STRING"))

def test_encode():

    def client():
        return requests.get("http://localhost:8000/あいう")
    
    env, res = util.run_client(client)
    assert(util.RESPONSE == res.content)
    assert("/あいう" == env["PATH_INFO"])
    assert(None == env.get("QUERY_STRING"))


def test_query():

    def client():
        return requests.get("http://localhost:8000/?a=1234&bbbb=ccc")
    
    env, res = util.run_client(client)
    assert(util.RESPONSE == res.content)
    assert("/" == env["PATH_INFO"])
    assert("a=1234&bbbb=ccc" == env["QUERY_STRING"])

def test_chunk_response():

    def client():
        return requests.get("http://localhost:8000/")
    
    env, res = util.run_client(client)
    headers = res.headers
    assert(util.RESPONSE == res.content)
    assert(headers["transfer-encoding"] == "chunked")
    assert(headers["connection"] == "close")

def test_err():

    def client():
        return requests.get("http://localhost:8000/")
    
    env, res = util.run_client(client, util.ErrApp)
    assert(500 == res.status_code)

def test_iter_err():

    def client():
        return requests.get("http://localhost:8000/")
    
    env, res = util.run_client(client, util.IterErrApp)
    assert(500 == res.status_code)

