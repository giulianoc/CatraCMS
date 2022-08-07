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
	moduleName=$1

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

	if [ "$moduleName" == "storage" ]; then

		#for storage just nfs is enougth
		apt -y install nfs-kernel-server

		return
	fi

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

	echo ""
	read -n 1 -s -r -p "install libtiff-dev..."
	echo ""
	apt-get -y install libtiff-dev

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
	read -n 1 -s -r -p "install libdc1394-dev..."
	echo ""
	apt-get -y install libdc1394-dev

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
	apt-get -y install libasound2-dev

	echo ""
	read -n 1 -s -r -p "install libpangocairo-1.0-0..."
	echo ""
	apt-get install -y libpangocairo-1.0-0

	#Per il transcoder sat
	echo ""
	read -n 1 -s -r -p "install dvb-tools..."
	echo ""
	apt install -y dvb-tools

	if [ "$moduleName" == "api" -o "$moduleName" == "integration" ]; then

		#api should have GUI as well

		echo ""
		read -n 1 -s -r -p "install jre..."
		echo ""
		apt install -y default-jre

		echo ""
		read -n 1 -s -r -p "install openjdk..."
		echo ""
		apt install -y openjdk-11-jdk
	fi

	if [ "$moduleName" == "engine" ]; then

		#api should have GUI as well

		echo ""
		read -n 1 -s -r -p "install mysql-client..."
		echo ""
		apt-get -y install mysql-client

		echo ""
		read -n 1 -s -r -p "install mysql-server..."
		echo ""
		apt-get -y install mysql-server

		echo ""
		echo -n "Type the DB name: "
		read dbName
		echo -n "Type the DB user: "
		read dbUser
		echo -n "Type the DB password: "
		read dbPassword
		echo "create database $dbName CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci" | mysql -u root -p$dbPassword
		echo "CREATE USER '$dbUser'@'%' IDENTIFIED BY '$dbPassword'" | mysql -u root -p$dbPassword
		echo "GRANT ALL PRIVILEGES ON *.* TO '$dbUser'@'%' WITH GRANT OPTION" | mysql -u root -p$dbPassword
		#grant process allows mysqldump
		echo "GRANT PROCESS ON *.* TO '$dbUser'@'%' WITH GRANT OPTION" | mysql -u root -p$dbPassword
		echo "CREATE USER '$dbUser'@'localhost' IDENTIFIED BY '$dbPassword'" | mysql -u root -p$dbPassword
		echo "GRANT ALL PRIVILEGES ON *.* TO '$dbUser'@'localhost' WITH GRANT OPTION" | mysql -u root -p$dbPassword
		echo "GRANT PROCESS ON *.* TO '$dbUser'@'localhost' WITH GRANT OPTION" | mysql -u root -p$dbPassword

		echo "Inside /etc/mysql/mysql.conf.d/mysqld.cnf change: bind-address, max_connections, sort_buffer_size, binlog_expire_logs_seconds"

		echo "Follow the instructions to change the datadir (https://www.digitalocean.com/community/tutorials/how-to-move-a-mysql-data-directory-to-a-new-location-on-ubuntu-18-04)"

		echo "Then restart mysql and run the SQL command: create table if not exists MMS_TestConnection (testConnectionKey BIGINT UNSIGNED NOT NULL AUTO_INCREMENT, constraint MMS_TestConnection_PK PRIMARY KEY (testConnectionKey)) ENGINE=InnoDB"
	fi
}

install-ftpserver()
{
	read -n 1 -s -r -p "install-ftpserver..."
	echo ""

	echo ""
	read -n 1 -s -r -p "update..."
	echo ""
	apt install vsftpd
	echo "in /etc/vsftpd.conf set"
	echo "anonymous_enable=NO"
	echo "local_enable=YES"
	echo "userlist_enable=YES"
	echo "userlist_deny=NO"
	echo "userlist_file=/etc/vsftpd_user_list"

	echo "write_enable=YES"
	echo "local_umask=022"
	#echo "chroot_local_user=YES"

	echo "#logging:"
	echo "dual_log_enable=YES"
	echo "xferlog_enable=YES"
	echo "xferlog_file=/var/catramms/logs/vsftpd/vsftpd_wuftp.log"
	echo "vsftpd_log_file=/var/catramms/logs/vsftpd/vsftpd_standard.log"
	echo "# If you want, you can have your log file in standard ftpd xferlog format"
	echo "xferlog_std_format=NO"
	echo "log_ftp_protocol=YES"

	echo "#timeouts in seconds"
	echo "idle_session_timeout=600"
	echo "data_connection_timeout=600"
		
	echo "#bytes al second"
	echo "local_max_rate=1024000"
	echo "#max_clients=1"
	echo "#max clients from the same IP"
	echo "max_per_ip=10"
	echo "#For security reason, it is allowed to STOR and RETR files but"
	echo "#it is not allowed to change the directory"
	echo "cmds_allowed=USER,PASS,SYST,TYPE,PWD,PORT,PASV,LIST,STOR,RETR,DELE,REST,MDTM,SIZE,QUIT"
	echo "abilitati all'FTP in /etc/vsftpd_user_list aggiungere solamente gli utenti abilitati all'FTP"

	echo "Add /sbin/nologin in /etc/shells"

	echo "listen_ipv6=NO"
	echo "listen=YES"
	echo "pasv_enable=Yes"
	echo "pasv_min_port=10090"
	echo "pasv_max_port=10100"
	echo "pasv_address=54.76.8.245"


	echo "To start the service at boot..."
	echo "systemctl enable vsftpd"
	echo "To restart the running service..."
	echo "systemctl restart vsftpd"


	echo "Per create un utente (i.e.: europa_tv)..."
	echo "recupero group id (of ftp group)..."
	echo "groupId=$(getent group ftp | cut -d':' -f3)"
	echo "adduser..."
	echo "adduser --gid $groupId --home /data/ftp-users/europa_tv europa_tv"
	echo "usermod europa_tv -s /sbin/nologin"
	echo "add user (europa_tv) to /etc/vsftpd_user_list"
}


create-directory()
{
	moduleName=$1

	read -n 1 -s -r -p "create-directory..."
	echo ""

	mkdir /opt/catramms

	mkdir /var/catramms
	mkdir /var/catramms/pids

	if [ "$moduleName" != "integration" ]; then
		mkdir /var/catramms/storage
		mkdir /var/catramms/storage/nginxWorkingAreaRepository
		mkdir /var/catramms/storage/MMSRepository
		if [ "$moduleName" == "encoder" ]; then
			ln -s /mnt/MMSTranscoderWorkingAreaRepository /var/catramms/storage/MMSTranscoderWorkingAreaRepository
		else
			mkdir /var/catramms/storage/MMSTranscoderWorkingAreaRepository
		fi
	fi

	ln -s /mnt/logs /var/catramms/logs
	chmod -R mms:mms /mnt/logs

	if [ "$moduleName" == "api" -o "$moduleName" == "integration" ]; then
		mkdir /var/catramms/logs/tomcat-gui
	fi
	if [ "$moduleName" == "api" ]; then
		mkdir /var/catramms/logs/mmsAPI
	fi
	if [ "$moduleName" == "encoder" -o "$moduleName" == "externalEncoder" ]; then
		mkdir /var/catramms/logs/mmsEncoder
	fi
	if [ "$moduleName" == "engine" ]; then
		mkdir /var/catramms/logs/mmsEngineService
	fi
	if [ "$moduleName" == "api" -o "$moduleName" == "encoder" -o "$moduleName" == "externalEncoder" -o "$moduleName" == "integration" ]; then
		mkdir /var/catramms/logs/nginx
	fi

	if [ "$moduleName" != "integration" ]; then
		mkdir /mnt/mmsStorage
		mkdir /mnt/mmsRepository0000
		chown mms:mms /mnt/mmsRepository0000
	fi

	read -n 1 -s -r -p "links..."
	echo ""

	if [ "$moduleName" == "externalEncoder" ]; then
		mkdir /mnt/mmsStorage/IngestionRepository
		chown mms:mms /mnt/mmsStorage/IngestionRepository
		mkdir /mnt/mmsStorage/MMSWorkingAreaRepository
		chown mms:mms /mnt/mmsStorage/MMSWorkingAreaRepository
		mkdir /mnt/mmsStorage/MMSRepository-free
		chown mms:mms /mnt/mmsStorage/MMSRepository-free
		mkdir /mnt/mmsStorage/MMSLive
		chown mms:mms /mnt/mmsStorage/MMSLive
	fi

	if [ "$moduleName" != "integration" ]; then
		#these links will be broken until the partition will not be mounted
		ln -s /mnt/mmsStorage/IngestionRepository /var/catramms/storage
		ln -s /mnt/mmsStorage/MMSGUI /var/catramms/storage
		ln -s /mnt/mmsStorage/MMSWorkingAreaRepository /var/catramms/storage
		ln -s /mnt/mmsStorage/dbDump /var/catramms/storage
		ln -s /mnt/mmsStorage/commonConfiguration /var/catramms/storage
		ln -s /mnt/mmsStorage/MMSRepository-free /var/catramms/storage
		#Assuming the partition for the first repository containing the media files is /mnt/mmsRepository0000
		ln -s /mnt/mmsRepository0000 /var/catramms/storage/MMSRepository/MMS_0000
		ln -s /mnt/mmsStorage/MMSLive /var/catramms/storage/MMSRepository

		ln -s /var/catramms/storage /home/mms
	fi

	ln -s /var/catramms/logs /home/mms

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
}

adds-to-bashrc()
{
	moduleName=$1

	read -n 1 -s -r -p "adds-to-bashrc..."
	echo ""

	read -n 1 -s -r -p ".bashrc..."
	echo ""
	echo -n "serverName for the 'bash prompt' (i.e. engine-db-1): "
	read serverName

	if [ "$moduleName" != "storage" ]; then
		echo "export PATH=\$PATH:~mms" >> /home/mms/.bashrc
		echo "alias encoderLog='vi \$(printLogFileName.sh encoder)'" >> /home/mms/.bashrc
		echo "alias engineLog='vi \$(printLogFileName.sh engine)'" >> /home/mms/.bashrc
		echo "alias apiLog='vi \$(printLogFileName.sh api)'" >> /home/mms/.bashrc
		echo "cat ~/MMS_RESTART.txt" >> /home/mms/.bashrc
	fi

	echo "alias h='history'" >> /home/mms/.bashrc
	echo "export EDITOR=/usr/bin/vi" >> /home/mms/.bashrc

	echo "PS1='$serverName-'\$PS1" >> /home/mms/.bashrc
	echo "date" >> /home/mms/.bashrc
}

install-mms-packages()
{
	moduleName=$1

	read -n 1 -s -r -p "install-mms-packages..."
	echo ""

	if [ "$moduleName" != "integration" ]; then
		package=jsoncpp
		read -n 1 -s -r -p "Downloading $package..."
		echo ""
		curl -o /opt/catramms/$package.tar.gz "https://mms-delivery-f.cibortv-mms.com/packages/$package.tar.gz"
		tar xvfz /opt/catramms/$package.tar.gz -C /opt/catramms
	fi


	if [ "$moduleName" != "integration" ]; then
		packageName=ImageMagick
		echo ""
		echo -n "$packageName version (i.e.: 7.0.10)? "
		read version
		package=$packageName-$version
		echo "Downloading $package..."
		curl -o /opt/catramms/$package.tar.gz "https://mms-delivery-f.cibortv-mms.com/packages/$package.tar.gz"
		tar xvfz /opt/catramms/$package.tar.gz -C /opt/catramms
		ln -rs /opt/catramms/$package /opt/catramms/$packageName
	fi


	if [ "$moduleName" != "integration" ]; then
		package=curlpp
		read -n 1 -s -r -p "Downloading $package..."
		echo ""
		curl -o /opt/catramms/$package.tar.gz "https://mms-delivery-f.cibortv-mms.com/packages/$package.tar.gz"
		tar xvfz /opt/catramms/$package.tar.gz -C /opt/catramms
	fi


	packageName=ffmpeg
	echo ""
	echo -n "$packageName version (i.e.: 4.4)? "
	read version
	package=$packageName-$version
	echo "Downloading $package..."
	curl -o /opt/catramms/$package.tar.gz "https://mms-delivery-f.cibortv-mms.com/packages/$package.tar.gz"
	tar xvfz /opt/catramms/$package.tar.gz -C /opt/catramms
	ln -rs /opt/catramms/$package /opt/catramms/$packageName


	packageName=nginx
	echo ""
	echo -n "$packageName version (i.e.: 1.19.4)? "
	read version
	package=$packageName-$version
	echo "Downloading $package..."
	curl -o /opt/catramms/$package.tar.gz "https://mms-delivery-f.cibortv-mms.com/packages/$package.tar.gz"
	tar xvfz /opt/catramms/$package.tar.gz -C /opt/catramms
	ln -rs /opt/catramms/$package /opt/catramms/$packageName

	#nginx configuration
	rm -rf /opt/catramms/nginx/logs
	ln -s /var/catramms/logs/nginx /opt/catramms/nginx/logs

	mv /opt/catramms/nginx/conf/nginx.conf /opt/catramms/nginx/conf/nginx.conf.backup
	ln -s /opt/catramms/CatraMMS/conf/nginx.conf /opt/catramms/nginx/conf/

	mkdir /opt/catramms/nginx/conf/sites-enabled

	if [ "$moduleName" == "load-balancer" ]; then
		ln -s /home/mms/mms/conf/catrammsLoadBalancer.nginx /opt/catramms/nginx/conf/sites-enabled/
	else
		ln -s /home/mms/mms/conf/catramms.nginx /opt/catramms/nginx/conf/sites-enabled/
	fi

	if [ "$moduleName" == "api" -o "$moduleName" == "integration" ]; then

		#api should have GUI as well

		echo ""
		echo -n "tomcat version (i.e.: 9.0.62)? Look the version at https://www-eu.apache.org/dist/tomcat"
		read VERSION
		wget https://www-eu.apache.org/dist/tomcat/tomcat-9/v${VERSION}/bin/apache-tomcat-${VERSION}.tar.gz -P /tmp
		tar -xvf /tmp/apache-tomcat-${VERSION}.tar.gz -C /opt/catramms
		ln -rs /opt/catramms/apache-tomcat-${VERSION} /opt/catramms/tomcat

		rm -rf /opt/catramms/tomcat/logs
		ln -s /var/catramms/logs/tomcat-gui /opt/catramms/tomcat/logs

		echo "<meta http-equiv=\"Refresh\" content=\"0; URL=/catramms/login.xhtml\"/>" > /opt/catramms/tomcat/webapps/ROOT/index.html

		chown -R mms:mms /opt/catramms/apache-tomcat-${VERSION}

		chmod u+x /opt/catramms/tomcat/bin/*.sh

		echo "[Unit]" > /etc/systemd/system/tomcat.service
		echo "Description=Tomcat 9 servlet container" >> /etc/systemd/system/tomcat.service
		echo "After=network.target" >> /etc/systemd/system/tomcat.service
		echo "" >> /etc/systemd/system/tomcat.service
		echo "[Service]" >> /etc/systemd/system/tomcat.service
		echo "Type=forking" >> /etc/systemd/system/tomcat.service
		echo "" >> /etc/systemd/system/tomcat.service
		echo "User=mms" >> /etc/systemd/system/tomcat.service
		echo "Group=mms" >> /etc/systemd/system/tomcat.service
		echo "" >> /etc/systemd/system/tomcat.service
		echo "Environment=\"JAVA_HOME=/usr/lib/jvm/java-11-openjdk-arm64\"" >> /etc/systemd/system/tomcat.service
		echo "Environment=\"JAVA_OPTS=-Djava.security.egd=file:///dev/urandom -Djava.awt.headless=true\"" >> /etc/systemd/system/tomcat.service
		echo "" >> /etc/systemd/system/tomcat.service
		echo "Environment=\"CATALINA_BASE=/opt/catramms/tomcat\"" >> /etc/systemd/system/tomcat.service
		echo "Environment=\"CATALINA_HOME=/opt/catramms/tomcat\"" >> /etc/systemd/system/tomcat.service
		echo "Environment=\"CATALINA_PID=/var/catramms/pids/tomcat.pid\"" >> /etc/systemd/system/tomcat.service
		echo "Environment=\"CATALINA_OPTS=-Xms512M -Xmx4096M -server -XX:+UseParallelGC\"" >> /etc/systemd/system/tomcat.service
		echo "" >> /etc/systemd/system/tomcat.service
		echo "ExecStart=/opt/catramms/tomcat/bin/startup.sh" >> /etc/systemd/system/tomcat.service
		echo "ExecStop=/opt/catramms/tomcat/bin/shutdown.sh" >> /etc/systemd/system/tomcat.service
		echo "" >> /etc/systemd/system/tomcat.service
		echo "[Install]" >> /etc/systemd/system/tomcat.service
		echo "WantedBy=multi-user.target" >> /etc/systemd/system/tomcat.service
		echo "" >> /etc/systemd/system/tomcat.service

		#notify systemd that a new unit file exists
		systemctl daemon-reload

		systemctl enable --now tomcat

		echo "Make sure inside tomcat/conf/server.xml we have:"
		echo ""
		echo "<Connector port=\"8080\" protocol=\"HTTP/1.1\""
		echo "address=\"127.0.0.1\""
		echo "connectionTimeout=\"20000\""
		echo "URIEncoding=\"UTF-8\""
		echo "redirectPort=\"8443\" />"
		echo ""
		echo "Make sure inside the Host tag we have:"
		echo ""
		echo "<Context path=\"/catramms\" docBase=\"catramms\" reloadable=\"true\">"
		echo "<WatchedResource>WEB-INF/web.xml</WatchedResource>"
		echo "</Context>"
		echo ""
		echo "copiare catramms.war in /opt/catramms/tomcat/webapps"
		echo "far partire tomcat in modo che crea la directory catramms"
		echo "ln -s /opt/catramms/tomcat/webapps/catramms/WEB-INF/classes/catramms.cloud.properties /opt/catramms/tomcat/conf/catramms.properties"
	fi

	if [ "$moduleName" != "integration" ]; then
		package=opencv
		read -n 1 -s -r -p "Downloading $package..."
		echo ""
		curl -o /opt/catramms/$package.tar.gz "https://mms-delivery-f.cibortv-mms.com/packages/$package.tar.gz"
		tar xvfz /opt/catramms/$package.tar.gz -C /opt/catramms
	fi


	if [ "$moduleName" != "integration" ]; then
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
		curl -o /opt/catramms/$package.tar.gz "https://mms-delivery-f.cibortv-mms.com/packages/$package.tar.gz"
		tar xvfz /opt/catramms/$package.tar.gz -C /opt/catramms
		ln -rs /opt/catramms/$package /opt/catramms/$packageName
	fi


	if [ "$moduleName" != "integration" ]; then
		packageName=CatraLibraries
		echo ""
		echo -n "$packageName version (i.e.: 1.0.150)? "
		read version
		package=$packageName-$version-ubuntu
		echo "Downloading $package..."
		curl -o /opt/catramms/$package.tar.gz "https://mms-delivery-f.cibortv-mms.com/packages/$package.tar.gz"
		tar xvfz /opt/catramms/$package.tar.gz -C /opt/catramms
		ln -rs /opt/catramms/$packageName-$version /opt/catramms/$packageName
	fi


	packageName=CatraMMS
	echo ""
	echo -n "$packageName version (i.e.: 1.0.2030)? "
	read version
	package=$packageName-$version-ubuntu
	echo "Downloading $package..."
	curl -o /opt/catramms/$package.tar.gz "https://mms-delivery-f.cibortv-mms.com/packages/$package.tar.gz"
	tar xvfz /opt/catramms/$package.tar.gz -C /opt/catramms
	ln -rs /opt/catramms/$packageName-$version /opt/catramms/$packageName


	packageName=aws-sdk-cpp
	echo ""
	package=$packageName
	echo "Downloading $package..."
	curl -o /opt/catramms/$package.tar.gz "https://mms-delivery-f.cibortv-mms.com/packages/$package.tar.gz"
	tar xvfz /opt/catramms/$package.tar.gz -C /opt/catramms

	if [ "$moduleName" == "externalEncoder" ]; then
		echo ""
		echo -n "Type the AWS Access Key Id: "
		read awsAccessKeyId
		echo ""
		echo -n "Type the AWS Secret Access Key: "
		read awsSecretAccessKey
		mkdir -p /home/mms/.aws
		echo "[default]" > /home/mms/.aws/credentials
		echo "aws_access_key_id = $awsAccessKeyId" >> /home/mms/.aws/credentials
		echo "aws_secret_access_key = $awsSecretAccessKey" >> /home/mms/.aws/credentials
	else
		ln -s /var/catramms/storage/commonConfiguration/.aws .
	fi


	if [ "$moduleName" == "encoder" -o "$moduleName" == "externalEncoder" ]; then
		if [ $externalEncoder -eq 1 ]; then
			packageName=externalEncoderMmsConf
		else
			packageName=encoderMmsConf
		fi
		echo ""
		package=$packageName
		echo "Downloading $package..."
		curl -o ~/$package.tar.gz "https://mms-delivery-f.cibortv-mms.com/packages/$package.tar.gz"
		tar xvfz ~/$package.tar.gz -C ~
	fi


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

	if [ "$moduleName" == "encoder" -o "$moduleName" == "externalEncoder" ]; then
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
		#ufw allow 3306
		ufw allow from 10.0.0.0/16 to any port 3306
	elif [ "$moduleName" == "load-balancer" ]; then
		# -> http(nginx) and https(nginx)
		ufw allow 80
		ufw allow 443
		ufw allow 8088
	elif [ "$moduleName" == "storage" ]; then
		ufw allow nfs
		#ufw allow 111
		ufw allow from 10.0.0.0/16 to any port 111
		#ufw allow 13035
		ufw allow from 10.0.0.0/16 to any port 13035
	fi

	ufw enable
	ufw status verbose

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

	#To block or deny all packets from 192.168.1.5
	#sudo ufw deny from 192.168.1.5 to any

	#Instead of deny rule we can reject connection from any IP
	#Reject sends a reject response to the source, while the deny
	#(DROP) target sends nothing at all.
	#sudo ufw reject from 192.168.1.5 to any

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
	echo "usage $0 <moduleName (load-balancer or engine or api or encoder or externalEncoder or storage or integration)>"

	exit
fi

moduleName=$1

#LEGGERE LEGGERE LEGGERE LEGGERE LEGGERE LEGGERE LEGGERE LEGGERE LEGGERE LEGGERE

#1. Per prima cosa: formattare e montare dischi se necessario
#       sudo fdisk /dev/nvme1n1 (p n p w)
#       sudo mkfs.ext4 /dev/nvme1n1p1
#2. Inizializzare /etc/fstab
#3. creare directory /logs /mnt/mmsRepository000???? /MMSTranscoderWorkingAreaRepository(solo in caso di encoder)
#4. apt-get -y install nfs-common
#5. sudo mount -a
#6 sudo su; ./installServer.sh <module>
#7. verificare ~/mms/conf/*
#8. remove installServer.sh
#9. remove ssh key from /home/ubuntu/.ssh/authorized_keys

ssh-port
mms-account-creation
time-zone
install-packages $moduleName

adds-to-bashrc $moduleName
if [ "$moduleName" == "storage" ]; then

	echo "- to avoid nfs to listen on random ports (we would have problems open the firewall):"
	echo "- open /etc/default/nfs-kernel-server"
	echo "-	comment out the line RPCMOUNTDOPTS=--manage-gids"
	echo "- add the following line RPCMOUNTDOPTS=\"-p 13035\""
	echo "- Restart NFSd with sudo systemctl restart nfs-kernel-server"
else
	echo ""
	create-directory $moduleName
	#install-mms-packages $moduleName
fi
firewall-rules $moduleName

if [ "$moduleName" == "storage" ]; then

	echo "- fdisk and mkfs to format the disks"
	echo "- mkdir /mnt/MMSRepository/MMS_XXXX"
	echo "- initialize /etc/fstab"
	echo "- mount -a"
	echo "- chown -R mms:mms /mnt/MMSRepository"
	echo "- initialize /etc/exports"
	echo "- exportfs -ra"
else
	echo ""
	echo "- copiare files in ~mms/ conf and scripts, see doc ... (scp -P 9255 mms/conf/* mms@135.125.97.201:~/mms/conf), check files and crontab -u mms ~/mms/conf/crontab.txt"
	echo ""
	echo "- in case of api/engine/load-balancer, initialize /etc/hosts"
	echo ""
	echo "- run the commands as mms user 'sudo mkdir /mnt/mmsRepository0001; sudo chown mms:mms /mnt/mmsRepository0001; ln -s /mnt/mmsRepository0001 /var/catramms/storage/MMSRepository/MMS_0001' for the others repositories"
	echo ""
	echo "- in case of the storage is just created and has to be initialized OR in case of an external transcoder, run the following commands (it is assumed the storage partition is /mnt/mmsStorage): mkdir /mnt/mmsStorage/IngestionRepository; mkdir /mnt/mmsStorage/MMSGUI; mkdir /mnt/mmsStorage/MMSWorkingAreaRepository; mkdir /mnt/mmsStorage/MMSRepository-free; mkdir /mnt/mmsStorage/MMSLive; mkdir /mnt/mmsStorage/dbDump; mkdir /mnt/mmsStorage/commonConfiguration; chown -R mms:mms /mnt/mmsStorage/*"
	echo ""
	echo "- in case it is NOT an external transcoder OR it is a nginx-load-balancer, in /etc/fstab add:"
	echo "10.24.71.41:zpool-127340/mnt/mmsStorage	/mmsStorage	nfs	rw,_netdev,mountproto=tcp	0	0"
	echo "for each MMSRepository:"
	echo "10.24.71.41:zpool-127340/mmsRepository0000	/mmsRepository0000	nfs	rw,_netdev,mountproto=tcp	0	0"
	echo "if the NAS Repository does not have the access to the IP of the new server, add it, go to the OVH Site, login to the CiborTV project, click on Server → NAS e CDN, Aggiungi un accesso per mmsStorage, Aggiungi un accesso per mmsRepository0000"
	echo ""
fi

if [ "$moduleName" == "encoder" -o "$moduleName" == "externalEncoder" ]; then
	echo "add the new hostname in every /etc/hosts of every api and engine servers"
fi

echo "if a temporary user has to be removed 'deluser test'"
echo ""
echo "Restart of the machine and connect as ssh -p 9255 mms@<server ip>"
echo ""


