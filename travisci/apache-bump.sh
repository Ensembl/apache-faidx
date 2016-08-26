# We need to run apache, then restart it to get coverage to
# work correctly

lcov --directory . --zerocounters
touch ${APACHE_FAIDX_DIR}/.libs/mod_faidx.gcda && chmod 666 ${APACHE_FAIDX_DIR}/.libs/mod_faidx.gcda && sudo chown root.root ${APACHE_FAIDX_DIR}/.libs/mod_faidx.gcda
ls -l ${APACHE_FAIDX_DIR}/
(sudo sh -c '. /etc/apache2/envvars; /usr/sbin/apache2 -X')&
sleep 1

echo "Apache started"
ps auxww|grep [a]pache2
curl "http://localhost/faidx/locations/human/"

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
