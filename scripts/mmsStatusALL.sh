#!/bin/bash

echo "MMS Encoder status"
~/mmsEncoder.sh status

echo "MMS API status"
~/mmsApi.sh status

echo "MMS Engine status"
~/mmsEngineService.sh status

echo "nginx status"
ps -ef | grep nginx

#echo "Tomcat status"
#~/tomcat.sh status

