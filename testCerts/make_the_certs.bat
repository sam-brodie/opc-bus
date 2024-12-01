rem rem make a root CA
rem "C:\Program Files\OpenSSL-Win64\bin\openssl.exe" req -x509 -newkey rsa:4096 -keyout ca/ca.key -out ca/ca.crt -sha256 -days 3650 -nodes -config ca.conf -subj "/C=de/ST=bayern/O=tum/OU=Brodie/CN=BrodieRootCa"
rem 
rem rem Make a request to sign an OPC UA server cert
rem "C:\Program Files\OpenSSL-Win64\bin\openssl.exe" req -new -newkey rsa:4096 -nodes -keyout sam_server_private_key.pem -config ca.conf -out serverreq.pem -subj "/C=de/ST=bayern/O=tum/OU=Brodie/CN=BrodieServer"
rem 
rem rem Make a request to sign an OPC UA client cert
rem "C:\Program Files\OpenSSL-Win64\bin\openssl.exe" req -new -newkey rsa:4096 -nodes -keyout sam_client_private_key.pem -out clientreq.pem -subj "/C=de/ST=bayern/O=tum/OU=Brodie/CN=BrodieClient" 
rem 
rem rem As the CA, sign the OPC UA server cert
rem "C:\Program Files\OpenSSL-Win64\bin\openssl.exe" ca -config ca.conf -out samServer_3_3.crt -infiles serverreq.pem 
rem 
rem rem As the CA, sign the OPC UA client cert
rem "C:\Program Files\OpenSSL-Win64\bin\openssl.exe" ca -config ca.conf -out samClient_3_3.crt -infiles clientreq.pem 
rem 
rem rem Make a CRL
rem "C:\Program Files\OpenSSL-Win64\bin\openssl.exe"  ca -config ca.conf -gencrl -keyfile ca/ca.key -cert ca/ca.crt -out root.crl.pem
rem "C:\Program Files\OpenSSL-Win64\bin\openssl.exe"  crl -inform PEM -in root.crl.pem -outform DER -out sam_CA_root_2_CRL.crl
rem 
rem rem -addext "subjectAltName = urn:open62541.client.application"



rem make a root CA
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe" req -x509 -newkey rsa:4096 -keyout ca/ca.key -out sam_CA_root_3.crt -sha256 -days 3650 -nodes -config ca.conf


rem Make a CRL
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe"  ca -config ca.conf -gencrl -keyfile ca/ca.key -cert sam_CA_root_3.crt -out root.crl.pem

"C:\Program Files\OpenSSL-Win64\bin\openssl.exe"  crl -inform PEM -in root.crl.pem -outform DER -out sam_CA_root_2_CRL.pem

rem Make a request to sign an OPC UA server cert
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe" genpkey -algorithm RSA -out sam_server_private_key.pem
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe" req -new -key sam_server_private_key.pem -out server.csr -config ca.conf
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe"  x509 -req -in server.csr -CA sam_CA_root_3.crt -CAkey  ca/ca.key -CAcreateserial -out samServer_3_3.crt -days 365 -extensions v3_server -extfile ca.conf
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe" x509 -in samServer_3_3.crt -text -noout

rem Make a request to sign an OPC UA client cert
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe" genpkey -algorithm RSA -out sam_client_private_key.pem
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe" req -new -key sam_client_private_key.pem -out client.csr -config ca.conf
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe"  x509 -req -in client.csr -CA sam_CA_root_3.crt -CAkey  ca/ca.key -CAcreateserial -out samClient_3_3.crt -days 365 -extensions v3_client -extfile ca.conf
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe" x509 -in samClient_3_3.crt -text -noout

rem copy /y ca/ca.crt 

