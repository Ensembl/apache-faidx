# We need to run apache, then restart it to get coverage to
# work correctly

(sudo sh -c '. /etc/apache2/envvars; /usr/sbin/apache2 -X')&

ps auxww|grep [a]pache2
curl "http://localhost/faidx/locations/human/"

sudo pkill --signal HUP apache2
(sudo sh -c '. /etc/apache2/envvars; /usr/sbin/apache2 -X')&
curl "http://localhost/faidx?set=human&location=1%3A1000-2000"
curl "http://localhost/faidx?set=human&location=Y%3A1000-2000"
curl "http://localhost/faidx/sets"
curl "http://localhost/faidx/locations/cat/"
curl "http://localhost/faidx/locations/human/"
lcov --directory . --capture --output-file coverage.info && lcov --list coverage.info

sudo pkill --signal HUP apache2
lcov --directory . --capture --output-file coverage.info && lcov --list coverage.info
(sudo sh -c '. /etc/apache2/envvars; /usr/sbin/apache2 -X')&
curl "http://localhost/faidx?set=human&location=1%3A1000-2000"
curl "http://localhost/faidx?set=human&location=Y%3A1000-2000"
curl "http://localhost/faidx/sets"
curl "http://localhost/faidx/locations/cat/"
curl "http://localhost/faidx/locations/human/"

sudo pkill --signal HUP apache2
sleep 5
lcov --directory . --capture --output-file coverage.info && lcov --list coverage.info
