#!/bin/bash

if [ $# -ne 1 -a $# -ne 2 ]
then
	echo "Usage $0 start | stop | status [nodaemon]"

	exit
fi

command=$1

if [ "$command" != "start" -a "$command" != "stop" -a "$command" != "status" ]
then
	echo "Usage $0 start | stop | status [nodaemon]"

	exit
fi

if [ $# -eq 2 -a "$2" != "nodaemon" ]
then
	echo "Usage $0 start | stop | status [nodaemon]"

	exit
fi


if [ "$2" == "nodaemon" ]
then
	FORK_OPTION="-n"
else
	FORK_OPTION=""
fi

CatraMMS_PATH=/opt/catramms

export LD_LIBRARY_PATH=$CatraMMS_PATH/CatraLibraries/lib:$CatraMMS_PATH/CatraMMS/lib:$CatraMMS_PATH/ImageMagick-7.0.8-10/lib:$CatraMMS_PATH/curlpp//lib:$CatraMMS_PATH/ffmpeg-4.0.2/lib:$CatraMMS_PATH/jsoncpp/lib:$CatraMMS_PATH/mysql-connector-c++-1.1.9-linux-ubuntu16.10-x86-64bit/lib:$CatraMMS_PATH/opencv/lib
export MMS_CONFIGPATHNAME=$CatraMMS_PATH/CatraMMS/conf/mms.rsi.cfg

PIDFILE=/var/catramms/pids/api.pid
PORT=8010

if [ "$command" == "start" ]
then
	spawn-fcgi -p $PORT -P $PIDFILE $FORK_OPTION $CatraMMS_PATH/CatraMMS/bin/cgi/api.fcgi
elif [ "$command" == "status" ]
then
	ps -ef | grep "api.fcgi" | grep -v grep | grep -v status
elif [ "$command" == "stop" ]
then
	#PIDFILE is not created in case of nodaemon
	kill -9 `cat $PIDFILE`
fi

