#!/bin/bash

#RUN AS ROOT

#each frequency corrispond to a transponder providing several channels

#tv directory contains dvblast configuration file managed (created and updated) by the mmsEncoder.
#	the name of these files are: <frequency>-<symbol rate>-<modulation>
#	the extension of the file could be:
#		- .txt: process is already up and running
#		- .changed: there was a change into the file to be managed
#When the mmsEncoder stops the channel will updates the content of the dvblast configuration file removing the configuration and leaving the file empty. This script, in this scenario, kills the process and remove the configuration file

tvChannelConfigurationDirectory=/var/catramms/tv
tvLogsChannelsDir=/var/catramms/logs/tv
#dvbChannelsPathName=/opt/catramms/CatraMMS/conf/3_UNIVERSAL.channel.dvbv5.conf
dvbChannelsPathName=/opt/catramms/CatraMMS/conf/3_terrestrian_2022_09_07.channel.dvbv5.conf
frontendToBeUsed=1

debug=1
debugFilename=/tmp/tvController.log


mkdir -p $tvChannelConfigurationDirectory
mkdir -p $tvLogsChannelsDir

#retention log file
threeDaysInMinutes=4320
find $tvLogsChannelsDir -mmin +$threeDaysInMinutes -type f -delete

getFreeDeviceNumber()
{
    tvFrequency=$1

	selectedDeviceNumber=255

	for deviceNumber in 0 1 2 3 4 5 6 7 8 9
	do
		#-x parameter just tunes and exit

		if [ $debug -eq 1 ]; then
			echo "getFreeDeviceNumber. dvbv5-zap $tvFrequency -a $deviceNumber -f $frontendToBeUsed -ss -x --all-pids -c $dvbChannelsPathName" >> $debugFilename
		fi
		dvbv5-zap $tvFrequency -a $deviceNumber -f $frontendToBeUsed -ss -x --all-pids -c $dvbChannelsPathName > /dev/null 2>&1
		if [ $? -eq 0 ]
		then
			selectedDeviceNumber=$deviceNumber

			break
		fi
	done

	if [ $debug -eq 1 ]; then
		echo "getFreeDeviceNumber. selectedDeviceNumber: $selectedDeviceNumber" >> $debugFilename
	fi

	return $selectedDeviceNumber
}

getActualDeviceNumber()
{
    tvFrequency=$1

	selectedDeviceNumber=255

	for deviceNumber in 0 1 2 3 4 5 6 7 8 9
	do
		pgrep -f "dvblast -f $tvFrequency -a $deviceNumber" > /dev/null
		pgrepStatus=$?
		if [ $pgrepStatus -eq 0 ]
		then
			#process was found
			selectedDeviceNumber=$deviceNumber

			break
		fi
	done

	if [ $debug -eq 1 ]; then
		echo "getActualDeviceNumber. selectedDeviceNumber: $selectedDeviceNumber" >> $debugFilename
	fi

	return $selectedDeviceNumber
}

startOfProcess()
{
    frequency=$1
    symbolRate=$2
    modulation=$3
    dvblastConfPathName=$4
	pidProcessPathName=$5

	getFreeDeviceNumber $frequency
	deviceNumber=$?
	if [ $deviceNumber -eq 255 ]
	then
		echo "No deviceNumber available"

		return 1
	fi

	modulationParameter=""
	if [ "$modulation" != "n.a." ]; then
		modulationParameter="-m $modulation"
	fi

	logPathName=$tvLogsChannelsDir/$frequency".log"
	if [ $debug -eq 1 ]; then
		echo "Start of the process. nohup dvblast -f $frequency -a $deviceNumber -s $symbolRate $modulationParameter -n $frontendToBeUsed -c $dvblastConfPathName > $logPathName 2>&1 &" >> $debugFilename
	fi

	nohup dvblast -f $frequency -a $deviceNumber -s $symbolRate $modulationParameter -n $frontendToBeUsed -c $dvblastConfPathName > $logPathName 2>&1 &
	echo $! > $pidProcessPathName
	#I saw it returned 0 even if the process failed. Better to check if the process is running
	#processReturn=$?
	sleep 5
	isProcessRunningFunc $frequency
	isProcessRunning=$?
	if [ $isProcessRunning -eq 0 ]
	then
		processReturn=1
	else
		processReturn=0
	fi

	return $processReturn
}

isProcessRunningFunc()
{
    frequency=$1

	pgrep -f "dvblast -f $frequency" > /dev/null
	if [ $? -eq 0 ]; then
		#process was found
		localIsProcessRunning=1
	else
		localIsProcessRunning=0
	fi
	if [ $debug -eq 1 ]; then
		echo "isProcessRunning: $localIsProcessRunning" >> $debugFilename
	fi

	return $localIsProcessRunning
}

# MAIN MAIN MAIN

if [ $debug -eq 1 ]; then
	echo "script started" >> $debugFilename
fi

configurationFiles=$(ls $tvChannelConfigurationDirectory)
for configurationFileName in $configurationFiles
do
	#for each <frequency>-<symbol rate>-<modulation>

	fileExtension=${configurationFileName##*.}
	if [ $debug -eq 1 ]; then
		echo "configurationFileName: $configurationFileName, fileExtension: $fileExtension" >> $debugFilename
	fi

	if [ "$fileExtension" == "txt" ]; then
		frequencySymbolRateModulation=$(basename $configurationFileName .txt)
	else
		frequencySymbolRateModulation=$(basename $configurationFileName .changed)
	fi
	frequency=$(echo $frequencySymbolRateModulation | cut -d'-' -f1)
	symbolRate=$(echo $frequencySymbolRateModulation | cut -d'-' -f2)
	modulation=$(echo $frequencySymbolRateModulation | cut -d'-' -f3)
	if [ "$modulation" == "" ]; then
		modulation="n.a."
	fi
	if [ $debug -eq 1 ]; then
		echo "frequency: $frequency, symbolRate: $symbolRate, modulation: $modulation" >> $debugFilename
	fi


	pidProcessPathName=/var/catramms/pids/tv_$frequency".pid"

	isProcessRunningFunc $frequency
	isProcessRunning=$?

	if [ "$fileExtension" == "txt" ]; then
		echo "No changes to $configurationFileName"

		if [ $isProcessRunning -eq 0 ]; then
			echo "Process is not up and running, start it"

			startOfProcess $frequency $symbolRate $modulation $tvChannelConfigurationDirectory/$configurationFileName $pidProcessPathName
		fi

		continue
	fi

	#fileExtension is 'changed'

	if [ $isProcessRunning -eq 1 ]; then
		if [ -s $pidProcessPathName ]; then

			if [ $debug -eq 1 ]; then
				echo "kill. pidProcessPathName: $(cat $pidProcessPathName)" >> $debugFilename
			fi

			kill -9 $(cat $pidProcessPathName) > /dev/null 2>&1
			echo "" > $pidProcessPathName

			sleep 1
		else
			echo "ERROR: process is running but there is no PID to kill it"

			continue
		fi
	fi

	fileSize=$(stat -c%s "$tvChannelConfigurationDirectory/$configurationFileName")
	#15 because we cannot have a conf less than 15 chars
	if [ $fileSize -lt 15 ]; then
		if [ $debug -eq 1 ]; then
			echo "dvblast configuration file is empty ($fileSize), channel is removed, configurationFileName: $configurationFileName" >> $debugFilename
		fi

		#process is alredy killed (see above statements)

		if [ $debug -eq 1 ]; then
			echo "rm -f $tvChannelConfigurationDirectory/$configurationFileName" >> $debugFilename
		fi
		rm -f $tvChannelConfigurationDirectory/$configurationFileName
	else
		startOfProcess $frequency $symbolRate $modulation $tvChannelConfigurationDirectory/$configurationFileName $pidProcessPathName
		processReturn=$?
		if [ $processReturn -eq 0 ]; then
			if [ $debug -eq 1 ]; then
				echo "mv $tvChannelConfigurationDirectory/$frequencySymbolRateModulation.changed $tvChannelConfigurationDirectory/$frequencySymbolRateModulation.txt" >> $debugFilename
			fi
			mv $tvChannelConfigurationDirectory/$frequencySymbolRateModulation".changed" $tvChannelConfigurationDirectory/$frequencySymbolRateModulation".txt"
		else
			if [ $debug -eq 1 ]; then
				echo "Start of the process failed, processReturn: $processReturn" >> $debugFilename
			fi
		fi
	fi

done

if [ $debug -eq 1 ]; then
	echo "script finished" >> $debugFilename
fi

