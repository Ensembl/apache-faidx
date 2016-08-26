# We need to run apache, then restart it to get coverage to
# work correctly

(sudo sh -c '. /etc/apache2/envvars; /usr/sbin/apache2 -X')&
sleep 2
ps auxww|grep [a]pache2
ps auxww|grep [a]pache2|awk '{print $2}'
ps auxww|grep [a]pache2|awk '{print $2}'|xargs -I % sudo kill -s HUP %
ps auxww|grep [a]pache2
(sudo sh -c '. /etc/apache2/envvars; /usr/sbin/apache2 -X')&
