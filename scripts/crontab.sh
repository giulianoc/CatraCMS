#!/bin/bash

export CatraMMS_PATH=/opt/catramms

#Retention (3 days: 4320 mins, 1 day: 1440 mins, 12 ore: 720 mins)
oneHourInMinutes=60
sixHourInMinutes=360
twelveHoursInMinutes=720
oneDayInMinutes=1440
twoDaysInMinutes=2880
threeDaysInMinutes=4320
fiveDaysInMinutes=7200
tenDaysInMinutes=14400
twentyDaysInMinutes=28800
sixMonthsInMinutes=259299

if [ $# -ne 1 -a $# -ne 2 -a $# -ne 3 ]
then
	echo "$(date): usage $0 <commandIndex> [<timeoutInMinutes>] [<db user> <db password>]" >> /tmp/crontab.log

    exit
fi

commandIndex=$1
timeoutInMinutes=$2
dbDetails=$3

if [ $commandIndex -eq 0 ]
then
	#update certificate

	#certbot path is different in case of ubuntu 18.04 or 20.04
	ubuntuVersion=$(cat /etc/lsb-release | grep -i RELEASE | cut -d'=' -f2 | cut -d'.' -f1)
	if [ $ubuntuVersion -eq 18 ]
	then
		sudo /usr/bin/certbot --quiet renew --pre-hook "$CatraMMS_PATH/CatraMMS/scripts/nginx.sh stop" --post-hook "$CatraMMS_PATH/CatraMMS/scripts/nginx.sh start"
	else
		export LD_LIBRARY_PATH=/opt/catramms/ffmpeg/lib && sudo certbot --quiet renew  --nginx-ctl /opt/catramms/nginx/sbin/nginx --nginx-server-root /opt/catramms/nginx/conf
	fi
else
	if [ $commandIndex -eq 1 ]
	then
		#first manage catalina.out file size if present
		file=/var/catramms/logs/tomcat-gui/catalina.out
		if [ -f "$file" ]
		then
			fileSizeInMegaBytes=$(du -m "$file" | awk '{print $1}')
			if [ $fileSizeInMegaBytes -gt 500 ]
			then
				echo "" > $file
			fi
		fi


		if [ "$timeoutInMinutes" == "" ]
		then
			timeoutInMinutes=$fiveDaysInMinutes
		fi

		commandToBeExecuted="find -L /var/catramms/logs/ -mmin +$timeoutInMinutes -type f -delete"
		timeoutValue="1h"
	elif [ $commandIndex -eq 2 ]
	then
		if [ "$timeoutInMinutes" == "" ]
		then
			timeoutInMinutes=$threeDaysInMinutes
		fi

		#serve per eliminare i file temporanei generati a causa di p:fileUpload non terminati.
		#Quelli che terminano vengono automaticamente eliminati.
		find /opt/catramms/tomcat/work/Catalina/localhost/catramms/ -mmin +$timeoutInMinutes -type f -delete

		commandToBeExecuted="find /var/catramms/storage/MMSGUI/temporaryPushUploads/ -mmin +$timeoutInMinutes -type f -delete"
		timeoutValue="1h"
	elif [ $commandIndex -eq 3 ]
	then
		if [ "$timeoutInMinutes" == "" ]
		then
			timeoutInMinutes=$threeDaysInMinutes
		fi

		# retention IngestionRepository for directories is nr. 8
		commandToBeExecuted="find /var/catramms/storage/IngestionRepository/ -mmin +$timeoutInMinutes -type f -delete"
		timeoutValue="1h"
	elif [ $commandIndex -eq 4 ]
	then
		if [ "$timeoutInMinutes" == "" ]
		then
			timeoutInMinutes=$twelveHoursInMinutes
		fi

		commandToBeExecuted="find /var/catramms/storage/MMSWorkingAreaRepository/ -mmin +$timeoutInMinutes -type f -delete"
		timeoutValue="1h"
	elif [ $commandIndex -eq 5 ]
	then
		if [ "$timeoutInMinutes" == "" ]
		then
			timeoutInMinutes=$threeDaysInMinutes
		fi

		find /var/catramms/storage/MMSTranscoderWorkingAreaRepository/ffmpegEndlessRecursivePlaylist/ -mmin +$sixMonthsInMinutes -type f -delete

		commandToBeExecuted="find /var/catramms/storage/MMSTranscoderWorkingAreaRepository/ffmpeg/ -mmin +$timeoutInMinutes -type f -delete"
		timeoutValue="1h"
	elif [ $commandIndex -eq 6 ]
	then
		if [ "$timeoutInMinutes" == "" ]
		then
			#2022-05-26: cambiato da 1 hour a 1 day because, in case of virtualVOD
			#of 2 hours, it was removing the segments when they were still in playlist
			timeoutInMinutes=$oneDayInMinutes
		fi

		commandToBeExecuted="find /var/catramms/storage/MMSTranscoderWorkingAreaRepository/Staging/ -mmin +$timeoutInMinutes -type f -delete"
		timeoutValue="1h"
	elif [ $commandIndex -eq 7 ]
	then
		if [ "$timeoutInMinutes" == "" ]
		then
			timeoutInMinutes=$oneHourInMinutes
		fi

		commandToBeExecuted="find /var/catramms/storage/MMSWorkingAreaRepository/Staging/ -mmin +$timeoutInMinutes -type f -delete"
		timeoutValue="1h"
	elif [ $commandIndex -eq 8 ]
	then
		if [ "$timeoutInMinutes" == "" ]
		then
			timeoutInMinutes=$threeDaysInMinutes
		fi

		# retention IngestionRepository for files is nr. 3
		commandToBeExecuted="find /var/catramms/storage/IngestionRepository/users/*/* -empty -mmin +$timeoutInMinutes -type d -delete"
		timeoutValue="1h"
	elif [ $commandIndex -eq 9 ]
	then
		if [ "$timeoutInMinutes" == "" ]
		then
			timeoutInMinutes=$sixHourInMinutes
		fi

		commandToBeExecuted="find /var/catramms/storage/MMSRepository/MMS_????/*/* -empty -mmin +$timeoutInMinutes -type d -delete"
		timeoutValue="1h"
	elif [ $commandIndex -eq 10 ]
	then
		if [ "$timeoutInMinutes" == "" ]
		then
			timeoutInMinutes=$oneHourInMinutes
		fi

		#2019-05-06: moved from 720 min to 360 min because we had the 'Argument list too long' error
		commandToBeExecuted="find /var/catramms/storage/MMSWorkingAreaRepository/Staging/* -empty -mmin +$timeoutInMinutes -type d -delete"
		timeoutValue="1h"
	elif [ $commandIndex -eq 11 ]
	then
		if [ "$timeoutInMinutes" == "" ]
		then
			#2022-05-26: cambiato da 1 hour a 1 day because, in case of virtualVOD
			#of 2 hours, it was removing the segments when they were still in playlist
			timeoutInMinutes=$oneDayInMinutes
		fi

		#2019-05-06: moved from 720 min to 360 min because we had the 'Argument list too long' error
		commandToBeExecuted="find /var/catramms/storage/MMSTranscoderWorkingAreaRepository/Staging/* -empty -mmin +$timeoutInMinutes -type d -delete"
		timeoutValue="1h"
	elif [ $commandIndex -eq 12 ]
	then
		DATE=$(date +%Y-%m-%d)
		DIRPATHNAME=/var/catramms/logs/nginx/$DATE
		if [ ! -d "$DIRPATHNAME" ]; then
			mkdir $DIRPATHNAME
			mv /var/catramms/logs/nginx/*.log $DIRPATHNAME

			#BE CAREFULL SUDO MAY ASK PASSWORD. 
			#Add the command '.../crontab.rsi.sh 12' to 'sudo crontab -e'
			#sudo kill -USR1 $(cat /var/catramms/pids/nginx.pid)
			kill -USR1 $(cat /var/catramms/pids/nginx.pid)
		fi

		if [ "$timeoutInMinutes" == "" ]
		then
			timeoutInMinutes=$tenDaysInMinutes
		fi

		commandToBeExecuted="find /var/catramms/logs/nginx/ -mmin +$timeoutInMinutes -type d -exec rm -rv {} +"
		timeoutValue="1h"
	elif [ $commandIndex -eq 13 ]
	then
		if [ "$timeoutInMinutes" == "" ]
		then
			timeoutInMinutes=$oneDayInMinutes
		fi

		commandToBeExecuted="find /var/catramms/storage/MMSRepository/MMSLive/* -mmin +$timeoutInMinutes -type f -delete"
		timeoutValue="1h"
	elif [ $commandIndex -eq 14 ]
	then
		if [ "$timeoutInMinutes" == "" ]
		then
			timeoutInMinutes=$oneDayInMinutes
		fi

		commandToBeExecuted="find /var/catramms/storage/MMSRepository/MMSLive/* -empty -mmin +$timeoutInMinutes -type d -delete"
		timeoutValue="1h"
	elif [ $commandIndex -eq 15 ]
	then
		if [[ "$timeoutInMinutes" == "" || "$timeoutInMinutes" == "0" ]]
		then
			timeoutInMinutes=$tenDaysInMinutes
		fi

		timeoutValue="1h"

		#2020-12-09: added / at the end of dumpDirectory (because it is a link,
		#'find' would not work)
		#dumpDirectory=/var/catramms/storage/dbDump/
		arrayOfDBUserPwd=($dbDetails)
		dbUserPwdNumber="${#arrayOfDBUserPwd[@]}"
		dbUserPwdIndex=0
		#echo $dbUserPwdNumber
		while [[ $dbUserPwdIndex -lt $dbUserPwdNumber ]]; do
			dbUser=${arrayOfDBUserPwd[$dbUserPwdIndex]}
			dbPwd=${arrayOfDBUserPwd[$((dbUserPwdIndex+1))]}
			dbName=${arrayOfDBUserPwd[$((dbUserPwdIndex+2))]}
			dumpDirectory=${arrayOfDBUserPwd[$((dbUserPwdIndex+3))]}

			dumpFileName=${dbUser}_$(date +"%Y-%m-%d").sql
			#echo $dbUser $dbPwd $dbName $dumpFileName
			mysqldump --no-tablespaces --single-transaction -u $dbUser -p$dbPwd -h db-slaves $dbName > $dumpDirectory$dumpFileName && gzip -f $dumpDirectory$dumpFileName

			dbUserPwdIndex=$((dbUserPwdIndex+4))

			#the retention command is called here because in case of multiple DB
			#the command would be called only for the last one
			commandToBeExecuted="find $dumpDirectory -mmin +$timeoutInMinutes -type f -delete"
			timeout $timeoutValue $commandToBeExecuted
		done

	else
		echo "$(date): wrong commandIndex: $commandIndex" >> /tmp/crontab.log

		exit
	fi

	timeout $timeoutValue $commandToBeExecuted
	if [ $? -eq 124 ]
	then
		echo "$(date): $commandToBeExecuted TIMED OUT" >> /tmp/crontab.log
	elif [ $? -eq 126 ]
	then
		echo "$(date): $commandToBeExecuted FAILED (Argument list too long)" >> /tmp/crontab.log
	fi
fi

