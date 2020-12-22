#!/bin/sh
#
# Demonstrates client persistence and the ability for the server to handle
# multiple clients.
echo
echo "************************************ TEST 2 ************************************"

echo
echo "*** Starting server (maxjobs=2)..."
rm -f .cse376hw4.socket
./bin/server 1> /dev/null 2> /dev/null &
SERVERPID=$!
sleep 1

echo
echo "*** Submitting 1 job as client 'asdf'"
./bin/client -u asdf -c "submit 10 123123123 12 df -h"
echo "*** Submitting 1 job as client 'asdf'"
./bin/client -u asdf -c "submit 20 123123123 10 sleep 10"
echo "*** Submitting 1 job as client 'asdf'"
./bin/client -u asdf -c "submit 20 212212212 8 sleep 12"

echo "*** Submitting 2 jobs as client 'qwerty'"
./bin/client -u qwerty -c "submit 4 321321312 8 du -h ."
./bin/client -u qwerty -c "submit 6 213213213 9 sleep 5"

echo
echo "*** Displaying job listing for 'asdf'"
./bin/client -u asdf -c "list"
echo "*** Displaying job listing for 'qwerty'"
./bin/client -u qwerty -c "list"

echo
echo "*** Waiting..."
sleep 15

echo
echo "*** Displaying job listing for 'asdf'"
./bin/client -u asdf -c "list"
echo "*** Displaying job listing for 'qwerty'"
./bin/client -u qwerty -c "list"

echo
echo "*** Shutting down server..."
/bin/kill -INT $SERVERPID
