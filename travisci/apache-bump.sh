# We need to run apache as the travis user, then stop it to get coverage to
# work correctly

lcov --directory . --zerocounters

# Set up the permissions to run as travis user
sudo chmod 666 /var/log/apache2/error.log
sudo chmod 666 /var/log/apache2/other_vhosts_access.log
sudo chmod 666 /var/log/apache2/access.log
sudo chmod 666 /var/run/apache2/apache2.pid
sudo chmod 777 /var/run/apache2/
sudo sed -i -e 's@80@8000@g' /etc/apache2/ports.conf
sudo sed -i -e 's@/var/log/apache2@/tmp@g' /etc/apache2/envvars
sudo sed -i -e 's@/var/lock/apache2@/tmp@g' /etc/apache2/envvars
sudo sed -i -e 's@/var/run/apache2@/tmp@g' /etc/apache2/envvars

# Start a single tread as the travis user
(. /etc/apache2/envvars; /usr/sbin/apache2 -X)&
sleep 2
ps auxww|grep [a]pache2

# Batch one of calls
curl "http://localhost:8000/faidx?set=human&location=1%3A1000-2000"
curl "http://localhost:8000/faidx?set=human&location=Y%3A1000-2000"
curl "http://localhost:8000/faidx/sets"
curl "http://localhost:8000/faidx/locations/cat/"
curl "http://localhost:8000/faidx/locations/human/"

apachectl -k graceful
sleep 1

# Batch two of calls
curl "http://localhost:8000/faidx?set=human&location=1%3A1000-2000"
curl -H "Content-type: text/x-fasta" "http://localhost:8000/faidx?set=human&location=Y%3A1000-2000"
curl "http://localhost:8000/faidx/sets"
curl "http://localhost:8000/faidx/locations/cat/"
curl "http://localhost:8000/faidx/locations/human/"

# Stop apache so it writes out the coverall output
apachectl -k stop
sleep 1

exit
