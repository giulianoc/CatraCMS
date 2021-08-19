#!/bin/bash

#'sudo su' before to run the command

ssh-port()
{
	read -n 1 -s -r -p "ssh port 9255..."
	echo ""

	echo "Port 9255" >> /etc/ssh/sshd_config
	/etc/init.d/ssh restart
}


mms-account-creation()
{
	read -n 1 -s -r -p "mms account creation..."
	echo ""

	echo "groupadd mms..."
	groupadd mms
	groupId=$(getent group mms | cut -d':' -f3)
	echo "adduser, groupId: $groupId..."
	adduser --gid $groupId mms

	#add temporary mms to sudoers in order to install and configure the server
	echo "usermod..."
	usermod -aG sudo mms

	#to change the password of root
	echo "Change the password of root..."
	passwd

	echo ".ssh initialization..."
	mkdir /home/mms/.ssh
	chmod 700 /home/mms/.ssh
	touch /home/mms/.ssh/authorized_keys
	chmod 600 /home/mms/.ssh/authorized_keys
	chown -R mms:mms /home/mms/.ssh

	read -n 1 -s -r -p "Add the authorized_keys..."
	vi /home/mms/.ssh/authorized_keys
	echo ""
}


time-zone()
{
	read -n 1 -s -r -p "set time zone..."
	echo ""

	timedatectl set-timezone Europe/Rome

	#Ubuntu uses by default using systemd's timesyncd service.Though timesyncd is fine for most purposes,
	#some applications that are very sensitive to even the slightest perturbations in time may be better served by ntpd,
	#as it uses more sophisticated techniques to constantly and gradually keep the system time on track
	#Before installing ntpd, we should turn off timesyncd:
	echo "turn off timesyncd..."
	timedatectl set-ntp no

	echo "update..."
	apt-get update

	echo "install ntp..."
	apt-get -y install ntp

	echo "to force the synchronization..."
	service ntp stop
	ntpd -gq
	service ntp start
}

install-packages()
{
	read -n 1 -s -r -p "install-packages..."
	echo ""

	echo ""
	read -n 1 -s -r -p "update..."
	echo ""
	apt update

	echo ""
	read -n 1 -s -r -p "upgrade..."
	echo ""
	apt -y upgrade

	echo ""
	read -n 1 -s -r -p "install build-essential git..."
	echo ""
	apt-get -y install build-essential git

	echo ""
	read -n 1 -s -r -p "install nfs-common..."
	echo ""
	apt-get -y install nfs-common

	echo ""
	read -n 1 -s -r -p "install libfcgi-dev..."
	echo ""
	apt-get -y install libfcgi-dev

	echo ""
	read -n 1 -s -r -p "install spawn-fcgi..."
	echo ""
	apt -y install spawn-fcgi

	#in order to compile CatraMMS (~/dev/CatraMMS) it is needed libcurl-dev:
	echo ""
	read -n 1 -s -r -p "install libcurl4-openssl-dev..."
	echo ""
	apt-get -y install libcurl4-openssl-dev

	echo ""
	read -n 1 -s -r -p "install curl..."
	echo ""
	apt-get install curl

	echo ""
	read -n 1 -s -r -p "install libjpeg-dev..."
	echo ""
	apt-get -y install libjpeg-dev

	echo ""
	read -n 1 -s -r -p "install libpng-dev..."
	echo ""
	apt-get -y install libpng-dev

	#used by ffmpeg:
	echo ""
	read -n 1 -s -r -p "install libxv1..."
	echo ""
	apt-get -y install libxv1

	echo ""
	read -n 1 -s -r -p "install libxcb-xfixes0-dev..."
	echo ""
	apt-get -y install libxcb-xfixes0-dev
	#apt-get -y install libsndio6.1 (non funziona con ubuntu 20)

	#This is to be able to compile CatraMMS (NOT install in case no compilation has to be done)
	#apt-get -y install --no-install-recommends libboost-all-dev

	#used by the opencv package
	echo ""
	read -n 1 -s -r -p "install libdc1394-22-dev..."
	echo ""
	apt-get -y install libdc1394-22-dev

	echo ""
	read -n 1 -s -r -p "install libmysqlcppconn-dev..."
	echo ""
	apt-get -y install libmysqlcppconn-dev

	echo ""
	read -n 1 -s -r -p "install libtiff5..."
	echo ""
	apt-get -y install libtiff5

	echo ""
	read -n 1 -s -r -p "install libfontconfig1..."
	echo ""
	apt-get -y install libfontconfig1

	echo ""
	read -n 1 -s -r -p "install libasound2-dev..."
	echo ""
	apt-get install libasound2-dev

	echo ""
	read -n 1 -s -r -p "install libpangocairo-1.0-0..."
	echo ""
	apt-get install -y libpangocairo-1.0-0

	#Per il transcoder sat
	echo ""
	read -n 1 -s -r -p "install dvb-tools..."
	echo ""
	apt install -y dvb-tools
}


create-directory()
{
	read -n 1 -s -r -p "create-directory..."
	echo ""

	mkdir /opt/catramms

	mkdir /var/catramms
	mkdir /var/catramms/pids

	mkdir /var/catramms/storage
	mkdir /var/catramms/storage/MMSTranscoderWorkingAreaRepository
	mkdir /var/catramms/storage/nginxWorkingAreaRepository
	mkdir /var/catramms/storage/MMSRepository

	mkdir /var/catramms/logs
	mkdir /var/catramms/logs/mmsAPI
	mkdir /var/catramms/logs/mmsEncoder
	mkdir /var/catramms/logs/mmsEngineService
	mkdir /var/catramms/logs/nginx
	mkdir /var/catramms/logs/tomcat-gui

	mkdir /mmsStorage
	mkdir /mmsRepository0000
	chown mms:mms /mmsRepository0000

	read -n 1 -s -r -p "links..."
	echo ""

	#these links will be broken until the partition will not be mounted
	ln -s /mmsStorage/IngestionRepository /var/catramms/storage
	ln -s /mmsStorage/MMSGUI /var/catramms/storage
	ln -s /mmsStorage/MMSWorkingAreaRepository /var/catramms/storage
	ln -s /mmsStorage/dbDump /var/catramms/storage
	ln -s /mmsStorage/commonConfiguration /var/catramms/storage
	ln -s /mmsStorage/MMSRepository-free /var/catramms/storage
	#Assuming the partition for the first repository containing the media files is /mmsRepository0000
	ln -s /mmsRepository0000 /var/catramms/storage/MMSRepository/MMS_0000
	ln -s /mmsStorage/MMSLive /var/catramms/storage/MMSRepository

	ln -s /var/catramms/logs /home/mms
	ln -s /var/catramms/storage /home/mms

	mkdir -p /home/mms/mms/conf
	mkdir -p /home/mms/mms/scripts

	ln -s /home/mms/mms/scripts/mmsStatusALL.sh /home/mms
	ln -s /home/mms/mms/scripts/mmsStartALL.sh /home/mms
	ln -s /home/mms/mms/scripts/mmsStopALL.sh /home/mms
	ln -s /opt/catramms/CatraMMS/scripts/nginx.sh /home/mms
	ln -s /opt/catramms/CatraMMS/scripts/mmsEncoder.sh /home/mms
	ln -s /opt/catramms/CatraMMS/scripts/mmsApi.sh /home/mms
	ln -s /opt/catramms/CatraMMS/scripts/mmsEngineService.sh /home/mms
	ln -s /opt/catramms/CatraMMS/scripts/mmsTail.sh /home/mms
	ln -s /opt/catramms/CatraMMS/scripts/tomcat.sh /home/mms
	ln -s /opt/catramms/CatraMMS/scripts/printLogFileName.sh /home/mms

	chown -R mms:mms /home/mms/mms

	chown -R mms:mms /opt/catramms
	chown -R mms:mms /var/catramms

	read -n 1 -s -r -p ".bashrc..."
	echo ""

	echo "export PATH=\$PATH:~mms" >> /home/mms/.bashrc
	echo "alias h='history'" >> /home/mms/.bashrc
	echo "alias encoderLog='vi \$(printLogFileName.sh encoder)'" >> /home/mms/.bashrc
	echo "alias engineLog='vi \$(printLogFileName.sh engine)'" >> /home/mms/.bashrc
	echo "alias apiLog='vi \$(printLogFileName.sh api)'" >> /home/mms/.bashrc
	echo "cat ~/MMS_RESTART.txt" >> /home/mms/.bashrc
}

install-mms-packages()
{
	moduleName=$1

	read -n 1 -s -r -p "install-mms-packages..."
	echo ""

	package=jsoncpp
	read -n 1 -s -r -p "Downloading $package..."
	echo ""
	curl -o /opt/catramms/$package.tar.gz "https://mms-delivery-f.restream.ovh/packages/$package.tar.gz"
	tar xvfz /opt/catramms/$package.tar.gz -C /opt/catramms


	packageName=ImageMagick
	echo ""
	echo -n "$packageName version (i.e.: 7.0.10)? "
	read version
	package=$packageName-$version
	echo "Downloading $package..."
	curl -o /opt/catramms/$package.tar.gz "https://mms-delivery-f.restream.ovh/packages/$package.tar.gz"
	tar xvfz /opt/catramms/$package.tar.gz -C /opt/catramms
	ln -rs /opt/catramms/$package /opt/catramms/$packageName


	package=curlpp
	read -n 1 -s -r -p "Downloading $package..."
	echo ""
	curl -o /opt/catramms/$package.tar.gz "https://mms-delivery-f.restream.ovh/packages/$package.tar.gz"
	tar xvfz /opt/catramms/$package.tar.gz -C /opt/catramms


	packageName=ffmpeg
	echo ""
	echo -n "$packageName version (i.e.: 4.4)? "
	read version
	package=$packageName-$version
	echo "Downloading $package..."
	curl -o /opt/catramms/$package.tar.gz "https://mms-delivery-f.restream.ovh/packages/$package.tar.gz"
	tar xvfz /opt/catramms/$package.tar.gz -C /opt/catramms
	ln -rs /opt/catramms/$package /opt/catramms/$packageName


	packageName=nginx
	echo ""
	echo -n "$packageName version (i.e.: 1.19.4)? "
	read version
	package=$packageName-$version
	echo "Downloading $package..."
	curl -o /opt/catramms/$package.tar.gz "https://mms-delivery-f.restream.ovh/packages/$package.tar.gz"
	tar xvfz /opt/catramms/$package.tar.gz -C /opt/catramms
	ln -rs /opt/catramms/$package /opt/catramms/$packageName

	#nginx configuration
	rm -rf /opt/catramms/nginx/logs
	ln -s /var/catramms/logs/nginx /opt/catramms/nginx/logs

	mv /opt/catramms/nginx/conf/nginx.conf /opt/catramms/nginx/conf/nginx.conf.backup
	ln -s /opt/catramms/CatraMMS/conf/nginx.conf /opt/catramms/nginx/conf/

	mkdir /opt/catramms/nginx/conf/sites-enabled

	if [ "$moduleName" == "load-balancer" ]; then
		ln -s ~/mms/conf/catrammsLoadBalancer.nginx /opt/catramms/nginx/conf/sites-enabled/
	else
		ln -s ~/mms/conf/catramms.nginx /opt/catramms/nginx/conf/sites-enabled/
	fi


	package=opencv
	read -n 1 -s -r -p "Downloading $package..."
	echo ""
	curl -o /opt/catramms/$package.tar.gz "https://mms-delivery-f.restream.ovh/packages/$package.tar.gz"
	tar xvfz /opt/catramms/$package.tar.gz -C /opt/catramms


	#Only in case we have to download it again, AS mms user
	#	mkdir /opt/catramms/youtube-dl-$(date +'%Y-%m-%d')
	#	curl -k -L https://yt-dl.org/downloads/latest/youtube-dl -o /opt/catramms/youtube-dl-$(date +'%Y-%m-%d')/youtube-dl
	#	chmod a+rx /opt/catramms/youtube-dl-$(date +'%Y-%m-%d')/youtube-dl
	#	rm /opt/catramms/youtube-dl; ln -s /opt/catramms/youtube-dl-$(date +'%Y-%m-%d') /opt/catramms/youtube-dl
	packageName=youtube-dl
	echo ""
	echo -n "$packageName version (i.e.: 2021-04-05)? "
	read version
	package=$packageName-$version
	echo "Downloading $package..."
	curl -o /opt/catramms/$package.tar.gz "https://mms-delivery-f.restream.ovh/packages/$package.tar.gz"
	tar xvfz /opt/catramms/$package.tar.gz -C /opt/catramms
	ln -rs /opt/catramms/$package /opt/catramms/$packageName


	packageName=CatraLibraries
	echo ""
	echo -n "$packageName version (i.e.: 1.0.24)? "
	read version
	package=$packageName-$version-ubuntu
	echo "Downloading $package..."
	curl -o /opt/catramms/$package.tar.gz "https://mms-delivery-f.restream.ovh/packages/$package.tar.gz"
	tar xvfz /opt/catramms/$package.tar.gz -C /opt/catramms
	ln -rs /opt/catramms/$packageName-$version /opt/catramms/$packageName


	packageName=CatraMMS
	echo ""
	echo -n "$packageName version (i.e.: 1.0.570)? "
	read version
	package=$packageName-$version-ubuntu
	echo "Downloading $package..."
	curl -o /opt/catramms/$package.tar.gz "https://mms-delivery-f.restream.ovh/packages/$package.tar.gz"
	tar xvfz /opt/catramms/$package.tar.gz -C /opt/catramms
	ln -rs /opt/catramms/$packageName-$version /opt/catramms/$packageName


	chown -R mms:mms /opt/catramms
}

firewall-rules()
{
	moduleName=$1

	read -n 1 -s -r -p "firewall-rules..."
	echo ""


	ufw default deny incoming
	ufw default allow outgoing

	#does not get the non-default port
	#ufw allow ssh
	ufw allow 9255

	if [ "$moduleName" == "encoder" ]; then
		#engine -> transcoder(nginx)
		ufw allow 8088

		#connection rtmp from public
		ufw allow 30000:31000/tcp
	elif [ "$moduleName" == "api" ]; then
		# -> http(nginx) and https(nginx)
		ufw allow 8088
		ufw allow 443
	elif [ "$moduleName" == "engine" ]; then
		# -> mysql
		ufw allow 3306
	elif [ "$moduleName" == "load-balancer" ]; then
		# -> http(nginx) and https(nginx)
		ufw allow 80
		ufw allow 443
		ufw allow 8088
	fi

	ufw enable

	#to delete a rule it's the same command to allow with 'delete' after ufw, i.e.: ufw delete allow ssh

	#to allow port ranges
	#ufw allow 6000:6007/tcp
	#ufw allow 6000:6007/udp

	#to allow traffic from a specific IP address (client)
	#ufw allow from 203.0.113.4

	#to allow traffic from a specific IP address (client) and a specific port
	#ufw allow from 203.0.113.4 to any port 22

	#to allow traffic from a subnet (client)
	#ufw allow from 203.0.113.0/24

	#to allow traffic from a subnet (client) and a specific port
	#ufw allow from 203.0.113.0/24 to any port 22

	#status of UFW
	#ufw status verbose

	#to disable the firewall
	#ufw disable

	#to enable again
	#ufw enable

	#This will disable UFW and delete any rules that were previously defined.
	#This should give you a fresh start with UFW.
	#ufw reset
}

if [ $# -ne 1 ]
then
	echo "usage $0 <moduleName (load-balancer or engine or api or encoder)>"

	exit
fi

moduleName=$1

#ssh-port
#mms-account-creation
#time-zone
#install-packages
#create-directory
#install-mms-packages $moduleName
firewall-rules $moduleName

echo ""
echo "- copiare files in ~mms/ conf and scripts, see doc ... (scp -P 9255 mms/conf/* mms@135.125.97.201:~/mms/conf), check files and crontab -u mms ~/mms/conf/crontab.txt"
echo ""
echo "- in case of api/engine/load-balancer, initialize /etc/hosts"
echo ""
echo "- run the commands as mms user 'sudo mkdir /mmsRepository0001; sudo chown mms:mms /mmsRepository0001; ln -s /mmsRepository0001 /var/catramms/storage/MMSRepository/MMS_0001' for the others repositories"
echo ""
echo "- in case of the storage is just created and has to be initialized OR in case of an external transcoder, run the following commands (it is assumed the storage partition is /mmsStorage): mkdir /mmsStorage/IngestionRepository; mkdir /mmsStorage/MMSGUI; mkdir /mmsStorage/MMSWorkingAreaRepository; mkdir /mmsStorage/MMSRepository-free; mkdir /mmsStorage/MMSLive; mkdir /mmsStorage/dbDump; mkdir /mmsStorage/commonConfiguration; chown -R mms:mms /mmsStorage/*"
echo ""
echo "- in case it is NOT an external transcoder OR it is a nginx-load-balancer, in /etc/fstab add:"
echo "10.24.71.41:zpool-127340/mmsStorage	/mmsStorage	nfs	rw,_netdev,mountproto=tcp	0	0"
echo "for each MMSRepository:"
echo "10.24.71.41:zpool-127340/mmsRepository0000	/mmsRepository0000	nfs	rw,_netdev,mountproto=tcp	0	0"
echo "if the NAS Repository does not have the access to the IP of the new server, add it, go to the OVH Site, login to the CiborTV project, click on Server → NAS e CDN, Aggiungi un accesso per mmsStorage, Aggiungi un accesso per mmsRepository0000"
echo ""

if [ "$moduleName" == "encoder" ]; then
	echo "add the new hostname in every /etc/hosts of every api and engine servers
fi

echo "if a temporary user has to be removed 'deluser test'"
echo ""
echo "Restart of the machine and connect as ssh -p 9255 mms@<server ip>"
echo ""


