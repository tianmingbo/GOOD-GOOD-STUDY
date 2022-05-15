<<<<<<< HEAD
import json
import requests
from requests.exceptions import ConnectionError
from accountpool.storages.redis import *
from accountpool.exceptions.init import InitException
from loguru import logger
=======
import requests
from requests.exceptions import ConnectionError
from storages.redis import *
from exceptions.init import InitException
from loguru import logger
from setting import TEST_URL_MAP
>>>>>>> 3bd9a02e0b0537a70c1b67d45c712c56bdb002f0


class BaseTester(object):
    """
    base tester
    """
<<<<<<< HEAD
    
=======

>>>>>>> 3bd9a02e0b0537a70c1b67d45c712c56bdb002f0
    def __init__(self, website=None):
        """
        init base tester
        """
        self.website = website
        if not self.website:
            raise InitException
        self.account_operator = RedisClient(type='account', website=self.website)
        self.credential_operator = RedisClient(type='credential', website=self.website)
<<<<<<< HEAD
    
=======

>>>>>>> 3bd9a02e0b0537a70c1b67d45c712c56bdb002f0
    def test(self, username, credential):
        """
        test single credential
        """
        raise NotImplementedError
<<<<<<< HEAD
    
=======

>>>>>>> 3bd9a02e0b0537a70c1b67d45c712c56bdb002f0
    def run(self):
        """
        test all credentials
        """
        credentials = self.credential_operator.all()
        for username, credential in credentials.items():
            self.test(username, credential)


class Antispider6Tester(BaseTester):
    """
    tester for antispider6
    """
<<<<<<< HEAD
    
    def __init__(self, website=None):
        BaseTester.__init__(self, website)
    
=======

    def __init__(self, website=None):
        BaseTester.__init__(self, website)

>>>>>>> 3bd9a02e0b0537a70c1b67d45c712c56bdb002f0
    def test(self, username, credential):
        """
        test single credential
        """
        logger.info(f'testing credential for {username}')
        try:
            test_url = TEST_URL_MAP[self.website]
            response = requests.get(test_url, headers={
                'Cookie': credential
            }, timeout=5, allow_redirects=False)
            if response.status_code == 200:
                logger.info('credential is valid')
            else:
                logger.info('credential is not valid, delete it')
                self.credential_operator.delete(username)
        except ConnectionError:
            logger.info('test failed')


class Antispider7Tester(BaseTester):
    """
    tester for antispider7
    """
<<<<<<< HEAD
    
    def __init__(self, website=None):
        BaseTester.__init__(self, website)
    
=======

    def __init__(self, website=None):
        BaseTester.__init__(self, website)

>>>>>>> 3bd9a02e0b0537a70c1b67d45c712c56bdb002f0
    def test(self, username, credential):
        """
        test single credential
        """
        logger.info(f'testing credential for {username}')
        try:
            test_url = TEST_URL_MAP[self.website]
            response = requests.get(test_url, headers={
                'authorization': f'jwt {credential}'
            }, timeout=5, allow_redirects=False)
            if response.status_code == 200:
                logger.info('credential is valid')
            else:
                logger.info('credential is not valid, delete it')
                self.credential_operator.delete(username)
        except ConnectionError:
            logger.info('test failed')
