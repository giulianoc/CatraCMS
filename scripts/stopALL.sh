#!/bin/bash


echo "mmsApi"
/opt/catramms/CatraMMS/scripts/mmsApi.sh stop

echo "mmsEncoder"
/opt/catramms/CatraMMS/scripts/mmsEncoder.sh stop

echo "mmsEngineService"
/opt/catramms/CatraMMS/scripts/mmsEngineService.sh stop

echo "tomcat"
/opt/catramms/CatraMMS/scripts/tomcat.sh stop

echo "nginx"
/opt/catramms/CatraMMS/scripts/nginx.sh stop

date
