rem make a root CA
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe" req -x509 -newkey rsa:2048 -keyout ca/ca.key -out sam_CA_root_3.crt -sha256 -days 7 -nodes -config ca.conf -subj "/C=de/ST=bayern/O=tum/OU=Brodie/CN=rootCA"


rem Make a CRL
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe"  ca -config ca.conf -gencrl -keyfile ca/ca.key -cert sam_CA_root_3.crt -out root.crl.pem
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe"  crl -inform PEM -in root.crl.pem -outform DER -out sam_CA_root_2_CRL.pem

rem Make a request to sign an OPC UA server cert
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe" genpkey -algorithm RSA -out sam_server_private_key.pem
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe" req -new -key sam_server_private_key.pem -out server.csr -config ca.conf -subj "/C=de/ST=bayern/O=tum/OU=Brodie/CN=opcServer" 
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe"  x509 -req -in server.csr -CA sam_CA_root_3.crt -CAkey  ca/ca.key -CAcreateserial -out samServer_3_3.crt -days 7 -extensions v3_server -extfile ca.conf
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe" x509 -in samServer_3_3.crt -text -noout

rem Make a request to sign an OPC UA client cert
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe" genpkey -algorithm RSA -out sam_client_private_key.pem
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe" req -new -key sam_client_private_key.pem -out client.csr -config ca.conf -subj "/C=de/ST=bayern/O=tum/OU=Brodie/CN=opcClient" 
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe"  x509 -req -in client.csr -CA sam_CA_root_3.crt -CAkey  ca/ca.key -CAcreateserial -out samClient_3_3.crt -days 7 -extensions v3_client -extfile ca.conf
"C:\Program Files\OpenSSL-Win64\bin\openssl.exe" x509 -in samClient_3_3.crt -text -noout
