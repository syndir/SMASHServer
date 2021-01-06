#!/bin/sh
#
# Demonstrates job submission and completion retrieval
echo
echo "************************************ TEST 1 ************************************"

echo
echo "*** Starting server..."
rm -f .smash.socket
./bin/server 1> /dev/null 2> /dev/null &
SERVERPID=$!
sleep 1

echo
echo "*** Submitting job..."
./bin/client -u asdf -c 'submit 10 123123123 12 ls -alR'
sleep 2

echo
echo "*** Retrieving job listing..."
./bin/client -u asdf -c 'list'
echo
echo "*** Retrieving standard output of job..."
./bin/client -u asdf -c 'stdout 0'
echo
echo "*** Retrieving standard err output of job..."
./bin/client -u asdf -c 'stderr 0'
echo
echo "*** Retrieving status of job..."
./bin/client -u asdf -c 'status 0'
echo
echo "*** Expunging job from server..."
./bin/client -u asdf -c 'expunge 0'
echo
echo "*** Retrieving job listing..."
./bin/client -u asdf -c 'list'

echo
echo "*** Submitting job 1 ..."
./bin/client -u asdf -c "submit 10 123123123 10 sleep 60"
./bin/client -u asdf -c "status 1"
echo
echo "*** Changing priority of job 1 from 10 to 14..."
./bin/client -u asdf -c "pri 1 14"
./bin/client -u asdf -c "status 1"

echo
echo "*** Submitting job 2..."
./bin/client -u asdf -c "submit 4 123123123 10 find / -name *.h"
echo
echo "*** Halting job 2..."
./bin/client -u asdf -c "stop 2"
./bin/client -u asdf -c "status 2"
echo
echo "*** Resuming job 2... "
./bin/client -u asdf -c "resume 2"
./bin/client -u asdf -c "status 2"

echo "*** Killing job 2... "
./bin/client -u asdf -c "kill 2"
./bin/client -u asdf -c "status 2"
echo
echo "*** stderr of job 2:"
./bin/client -u asdf -c "stderr 2"

echo
echo "*** Submitting job 3 (with limits) and waiting"
./bin/client -u asdf -c "submit 1 123123123 10 find / -name *.h"
sleep 60
./bin/client -u asdf -c "status 3"

echo
echo "*** Shutting down server..."
/bin/kill -INT $SERVERPID
