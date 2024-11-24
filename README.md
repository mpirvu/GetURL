# GetURL
App to issue http requests continuously from several 
concurrent simulated clients.

To compile:
   make
Note: you must jave the cURL library installed


To run:
./geturl [Options] -s url_to_access

Options:
-c ClientNum       Number of clients to simulate. Default is 1
-d Delay           Think time between requests (ms). Default is 0
-p PeriodicalStats Number of seconds for printing stats periodically. Default is 5 sec
-r RepeatCount     How many times each client issues a request. Default is forever
-t TestDuration    Expressed in seconds. Default is forever


