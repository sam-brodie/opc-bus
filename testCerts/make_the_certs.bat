rem make a root CA
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe" req -x509 -newkey rsa:4096 -keyout ca/ca.key -out ca/ca.crt -sha256 -days 3650 -nodes -subj "/C=de/ST=bayern/O=tum/OU=Brodie/CN=BrodieRootCa"

rem Make a request to sign an OPC UA server cert
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe" req -new -newkey rsa:4096 -nodes -keyout serverkey.pem -out serverreq.pem -subj "/C=de/ST=bayern/O=tum/OU=Brodie/CN=BrodieServer"

rem Make a request to sign an OPC UA client cert
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe" req -new -newkey rsa:4096 -nodes -keyout clientkey.pem -out clientreq.pem -subj "/C=de/ST=bayern/O=tum/OU=Brodie/CN=BrodieClient"

rem As the CA, sign the OPC UA server cert
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe" ca -config ca.conf -out ServerCertificate.pem.crt -infiles serverreq.pem 

rem As the CA, sign the OPC UA client cert
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe" ca -config ca.conf -out ClientCertificate.pem.crt -infiles clientreq.pem 