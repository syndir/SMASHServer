#!/bin/sh
#
# Demonstrates the "max jobs" functionality of the server
echo
echo "************************************ TEST 5 ************************************"

echo
echo "*** Starting server (maxjobs=2)..."
rm -f .cse376hw4.socket
./bin/server -n 2 1>/dev/null 2>/dev/null &
SERVERPID=$!
sleep 1

echo
echo "*** Client submitting job 1 as 'asdf'..."
./bin/client -u asdf -c "submit 60 123123123 12 sleep 10"
echo
echo "*** Client submitting  job as 'qwerty'..."
./bin/client -u qwerty -c "submit 30 212212212 9 sleep 7"
echo
echo "*** Client submitting job 2 as 'asdf'..."
./bin/client -u asdf -c "submit 60 213213213 11 sleep 12"
echo
echo "*** Client submitting job 3 as 'asdf'..."
./bin/client -u asdf -c "submit 60 312312312 10 sleep 15"
echo
echo "*** Client sumitting another job as 'qwerty'..."
./bin/client -u qwerty -c "submit 30 212212212 9 sleep 30"
echo
echo "*** Status listing of asdf's jobs..."
./bin/client -u asdf -c "list"
echo
echo "*** Status listing of qwerty's jobs..."
./bin/client -u qwerty -c "list"

echo
echo "*** Waiting..."
sleep 11
echo
echo "*** Status listing of asdf's jobs..."
./bin/client -u asdf -c "list"
echo
echo "*** Status listing of qwerty's jobs..."
./bin/client -u qwerty -c "list"


echo
echo "*** Waiting..."
sleep 19
echo
echo "*** Status listing of asdf's jobs..."
./bin/client -u asdf -c "list"
echo
echo "*** Status listing of qwerty's jobs..."
./bin/client -u qwerty -c "list"

echo
echo "*** Shutting down server..."
/bin/kill -INT $SERVERPID
