# We need to run apache as the travis user, then stop it to get coverage to
# work correctly

lcov --directory . --zerocounters
ls -l ${APACHE_FAIDX_DIR}/.libs/

# Set up the permissions to run as travis user
sudo chmod 666 /var/log/apache2/error.log
sudo chmod 666 /var/log/apache2/other_vhosts_access.log
sudo chmod 666 /var/log/apache2/access.log
sudo chmod 666 /var/log/apache2/error.log
sudo chmod 666 /var/run/apache2/apache2.pid
sudo chmod 777 /var/run/apache2/
sudo chmod 777 /var/log/apache2/
sudo sed -i -e 's@80@8000@g' /etc/apache2/ports.conf
sudo sed -i -e 's@/var/log/apache2@/tmp@g' /etc/apache2/envvars
sudo sed -i -e 's@/var/lock/apache2@/tmp@g' /etc/apache2/envvars
sudo sed -i -e 's@/var/run/apache2@/tmp@g' /etc/apache2/envvars

sudo a2dismod mpm_event
sudo a2denod mpm_prefork

echo "env variables"
cat /etc/apache2/envvars

# Start a single tread as the travis user
(. /etc/apache2/envvars; /usr/sbin/apache2 -X)&
sudo sync
sleep 5
ps auxww|grep [a]pache2
tail /var/log/apache2/error.log
tail /tmp/error.log

# Batch one of calls
echo "Batch one"
python ${APACHE_FAIDX_DIR}/test/api_test.py

apachectl -k graceful
sudo sync
sleep 2
ls -l ${APACHE_FAIDX_DIR}/.libs/

# Batch two of calls
echo "Batch two"
python ${APACHE_FAIDX_DIR}/test/api_test.py

apachectl -k graceful
sudo sync
sleep 5
ls -l ${APACHE_FAIDX_DIR}/.libs/

# Batch three of calls
echo "Batch three"
python ${APACHE_FAIDX_DIR}/test/api_test.py

# Stop apache so it writes out the coverall output
apachectl -k stop
sudo sync
sleep 5
ls -l ${APACHE_FAIDX_DIR}/.libs/

exit 0

(sudo sh -c '. /etc/apache2/envvars; /usr/sbin/apache2 -X')&
sleep 1

echo "Apache started"
ps auxww|grep [a]pache2
curl "http://localhost/faidx/metadata/2648ae1bacce4ec4b6cf337dcae37816"

echo "Killing apache"
sudo pkill -n --signal HUP apache2
sleep 1
ls -l ${APACHE_FAIDX_DIR}/.libs/

echo "Starting apache"
(sudo sh -c '. /etc/apache2/envvars; /usr/sbin/apache2 -X')&
curl "http://localhost/faidx?set=human&location=1%3A1000-2000"
curl "http://localhost/faidx?set=human&location=Y%3A1000-2000"
curl "http://localhost/faidx/sets"
curl "http://localhost/faidx/locations/cat/"
curl "http://localhost/faidx/locations/human/"
lcov --directory . --capture --output-file coverage.info && lcov --list coverage.info

echo "Killing apache"
sudo pkill -n --signal HUP apache2
sleep 1

lcov --directory . --capture --output-file coverage.info && lcov --list coverage.info

echo "Starting Apache"
(sudo sh -c '. /etc/apache2/envvars; /usr/sbin/apache2 -X')&
curl "http://localhost/faidx?set=human&location=1%3A1000-2000"
curl "http://localhost/faidx?set=human&location=Y%3A1000-2000"
curl "http://localhost/faidx/sets"
curl "http://localhost/faidx/locations/cat/"
curl "http://localhost/faidx/locations/human/"

echo "Killing apache"
sudo pkill -n --signal HUP apache2
sleep 5
lcov --directory . --capture --output-file coverage.info && lcov --list coverage.info
