if [ $# -ne 2 ];
then
    echo "Usage $0 <startDate (2022-09-01 00:00:00)> <endDate (2022-09-02 00:00:00)>"

    exit 1
fi

debug=1

rowsBeforeCommand=$(echo "select count(*) from MMS_RequestStatistic where workspaceKey = 1 and requestTimestamp >= '$1' and requestTimestamp < '$2';" | mysql -N -u mms -pF_-A*kED-34-r*U -h db-server-active mms 2> /dev/null)

startInSeconds=$(date +%s -d "$1")
endInSeconds=$(date +%s -d "$2")

secondsDatesTooClose=5

#10 minutes
periodInSeconds=600

currentInSeconds=$startInSeconds

startCommand=$(date +%s)
while [ $currentInSeconds -lt $endInSeconds ]; do
        startFormatted=$(date -d @$currentInSeconds +"%Y-%m-%d %H:%M:%S")

        currentInSecondsForNestedSelect=$((currentInSeconds-secondsDatesTooClose))
        startFormattedForNestedSelect=$(date -d @$currentInSecondsForNestedSelect +"%Y-%m-%d %H:%M:%S")

        currentInSeconds=$((currentInSeconds+periodInSeconds))

        endFormatted=$(date -d @$currentInSeconds +"%Y-%m-%d %H:%M:%S")

		if [ $debug -eq 1 ]; then
			sqlCommand="CREATE TEMPORARY TABLE t SELECT A.requestStatisticKey FROM MMS_RequestStatistic AS A WHERE A.workspaceKey = 1 AND A.requestTimestamp between '$startFormatted' AND '$endFormatted' AND EXISTS (SELECT B.requestStatisticKey FROM MMS_RequestStatistic AS B WHERE B.workspaceKey = 1 and A.userId = B.userId AND B.requestTimestamp between '$startFormattedForNestedSelect' AND '$endFormatted' AND B.requestTimestamp <= DATE_ADD(A.requestTimestamp, INTERVAL $secondsDatesTooClose SECOND) AND B.requestTimestamp > A.requestTimestamp); SELECT COUNT(*) from t; drop temporary table t;"
		else
			sqlCommand="CREATE TEMPORARY TABLE t SELECT A.requestStatisticKey FROM MMS_RequestStatistic AS A WHERE A.workspaceKey = 1 AND A.requestTimestamp between '$startFormatted' AND '$endFormatted' AND EXISTS (SELECT B.requestStatisticKey FROM MMS_RequestStatistic AS B WHERE B.workspaceKey = 1 and A.userId = B.userId AND B.requestTimestamp between '$startFormattedForNestedSelect' AND '$endFormatted' AND B.requestTimestamp <= DATE_ADD(A.requestTimestamp, INTERVAL $secondsDatesTooClose SECOND) AND B.requestTimestamp > A.requestTimestamp); SELECT COUNT(*) from t; delete from MMS_RequestStatistic where requestStatisticKey in (select requestStatisticKey from t); drop temporary table t;"
		fi

		if [ $debug -eq 1 ]; then
			echo $sqlCommand
		fi
        startSqlCommand=$(date +%s)
        echo $sqlCommand | mysql -N -u mms -pF_-A*kED-34-r*U -h db-server-active mms 2> /dev/null
        endSqlCommand=$(date +%s)
		if [ $debug -eq 1 ]; then
			echo "$startFormatted   -   $endFormatted -> $((endSqlCommand-startSqlCommand)) seconds"
		fi

done

rowsAfterCommand=$(echo "select count(*) from MMS_RequestStatistic where workspaceKey = 1 and requestTimestamp >= '$1' and requestTimestamp < '$2';" | mysql -N -u mms -pF_-A*kED-34-r*U -h db-server-active mms 2> /dev/null)

endCommand=$(date +%s)
echo "$1   -   $2 -> $((endCommand-startCommand)) seconds, removed $((rowsAfterCommand-rowsBeforeCommand)) ($rowsAfterCommand-$rowsBeforeCommand) rows"

