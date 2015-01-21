ECEN602 HW2 Programming Assignment (TFTP protocol implementation)
-----------------------------------------------------------------

Team Number: 28
Member 1 # Singh, Jasmeet (UIN: 523005618)
Member 2 # Zhang, Jiahao  (UIN: 723005723)
---------------------------------------

Description/Comments:
--------------------

Packet types implemented:
1) RRQ
2) DATA
3) ACK
4) ERROR

- We have made a data structure named TFTP, which can store a decoded TFTP packet.
- There are two functions called encode and decode. Encode takes packet type, block, data and it's length as argument and returns a TFTP packet. 
- Decode function takes a received packet and decodes its contents into the TFTP structure mentioned above.

Server:
1) Server is started on the user input IP address and Port number. The server binds to this IP address and Port number permanently to listen for new requests.
2) Whenever the server receives a new request (RRQ Packet), the server creates a child process using fork() to handle the new client separately.
3) The file requested by the user is opened and it's size and the packets it will consume is calculated. 
4) In the child process the server creates a new socket and binds it to a random port number for further communication with the client.
5) At this stage if the file could not be opened due to some reason an Error is sent back to client with Error Code 1.
6) After that the server sequentially sends each block or packet and waits for its acknowledgement.
7) SELECT structure is used to create a timeout every 100ms if no Acknowledgment is received from the client and then the last packet is retransmitted.
8) A maximum of 50 consecutive retransmits are permitted after which the server ends the connection.
9) Once all data is sent successfully, or a complete timeout occurs, the child process is ended.
10) If the size of the file is exact multiple of 512 bytes, an extra packet is sent in the end with no data (only opcode and block), which will confirm the client about the end of further packets.


Unix command for starting server:
------------------------------------------
./server SERVER_IP SERVER_PORT
