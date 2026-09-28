#!/usr/bin/env python2
'''
Created on Jul 11, 2013

@author: MasterGeek
'''

import json
import sys
import os
import struct
import threading
import time
import random
import urllib
import urllib2
import traceback
from array import array
from socket import *

SERVER_PORT = int(os.environ.get("P2K_TOURNAMENT_PORT", "2069"))
BUFSIZE = 1024

pkt_resp_main_info              = bytearray([0xff,0xfd])
pkt_resp_div_info               = bytearray([0xff,0xfc])
pkt_resp_power_on               = bytearray([0xff,0xf7])
pkt_resp_player_info            = bytearray([0xff,0xf8])
pkt_resp_picture                = bytearray([0xff,0xf9])
pkt_resp_div_players            = bytearray([0xff,0xfb])
pkt_resp_qualified              = bytearray([0xff,0xff])


main_info =                       [ 0xff, 0xfd, 0x17, 0x21, 0x00, 0x00, 0x00, 0x2a,
                                    0x50, 0x6c, 0x61, 0x79, 0x70, 0x69, 0x6e, 0x62, 0x6f,
                                    0x78, 0x2e, 0x63, 0x6f, 0x6d, 0x20, 0x57, 0x6f, 0x72,
                                    0x6c, 0x64, 0x20, 0x54, 0x6f, 0x75, 0x72, 0x6e, 0x61,
                                    0x6d, 0x65, 0x6e, 0x74, 0x00,
                                    0x00, 0x02]
# Change the last 2 bytes if there are qualified players in these divisions
div1 =                            [ 0xff, 0xfc, 0x17, 0x22, 0x00, 0x00, 0x00, 0x2a,
                                    0x52, 0x65, 0x76, 0x65, 0x6e, 0x67, 0x65, 0x20, 0x46, 0x72, 0x6f, 0x6d, 0x20, 0x4d, 0x61, 0x72, 0x73, 0x00,
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00 ]

div2 =                            [ 0xff, 0xfc, 0x17, 0x22, 0x00, 0x00, 0x00, 0x2a,
                                    0x53, 0x74, 0x61, 0x72, 0x20, 0x57, 0x61, 0x72, 0x73, 0x20, 0x45, 0x70, 0x69, 0x73, 0x6f, 0x64, 0x65, 0x20, 0x49,
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00 ]

self_test =                       [ 0xff, 0xf7, 0x17, 0x1f, 0x00, 0x00, 0x00, 0x08 ]

no_players =                      bytearray([
                                             0xff, 0xfb, 0x17, 0x23, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00
                                             ])

div_3_players =                   bytearray([
                                             0xff, 0xfb, 0x17, 0x23, 0x00, 0x00, 0x00, 0x18,
                                             0x00, 0x03, 0x00, 0x00,
                                             0x00, 0x98, 0x9a, 0x6a,
                                             0x00, 0x00, 0x03, 0xe9,
                                             0x00, 0x98, 0x9a, 0x6b
                                                        ])

qualified_no =                    bytearray([
                                             0xff, 0xff, 0x23, 0x5f, 0x00, 0x00, 0x00, 0x09, 0x00
                                             ])

server_packet_num = 0
sock = None
game_id = None
current_player_name = ""
game_name = ""
last_score_pulldown_time = 0

#####################################################################

divisions = [{"id": 0, "name": "Revenge From Mars", "players": []},
             {"id": 1, "name": "Star Wars Episode I", "players": []}]

tournament_name = "Pinbox World Tournament"

API_HOST = os.environ.get("P2K_TOURNAMENT_API",
                          "http://playpinbox.com/index.php/api/")
if not API_HOST.endswith("/"):
    API_HOST += "/"

def get_player_name(pid):
    global divisions
    for division in divisions:
        for player in division["players"]:
            if player["id"] == pid:
                return player["name"]

    return ""

def get_player_id(pname):
    global divisions
    for division in divisions:
        for player in division["players"]:
            if player["name"] == pname:
                return player["id"]

    return 0

def get_player(pid):
    global divisions
    for division in divisions:
        for player in division["players"]:
            if player["id"] == pid:
                return player

    return {}

def get_division(divid):
    global divisions
    for division in divisions:
        if division["id"] == divid:
            return division

    return None;

def get_player_division(playerid):
    global divisions
    for division in divisions:
        for player in division["players"]:
            if player["id"] == playerid:
                return division["name"]


def bytes_to_string(buf):
    return "".join(map(chr, buf))

def bytes_to_int(buf):
    buf = buf[::-1]
    S = struct.Struct('<I')
    return S.unpack_from(buffer(bytearray(buf)))[0]

def buf_to_string(buf):

    hexdata = ' '.join('%02x' % ord(byte) for byte in buf)

    return hexdata

def get_next_seq_num():
    global server_packet_num
    server_packet_num += 1
    if server_packet_num >= 65534:
        server_packet_num = 1

    arr = [server_packet_num >> i & 0xff for i in (24,16,8,0)]

    #if sys.byteorder == 'little':
    #    arr = arr[::-1]

    return (arr[2],arr[3])

def int_to_bytes(intval):
    arr = [intval >> i & 0xff for i in (24,16,8,0)]

    #if sys.byteorder == 'little':
    #    arr = arr[::-1]

    return arr

def int_to_bytes8(intval):
    arr = [intval >> i & 0xff for i in (56,48,40,32,24,16,8,0)]

    return arr

def string_to_bytes(string):
    return bytearray(string)

def copy_array_padded(buf,pad_to):
    result = [0] * pad_to
    c = 0
    for byte in buf:
        result[c] = byte
        c += 1

    return result

def get_game_id():
    global game_id

    if game_id != None:
        return game_id

    if os.path.exists(".gameid"):
        f = open(".gameid","r");
        game_id = f.readline().strip()
        f.close()
        return game_id

    if os.path.exists("/sys/class/net/eth0/address"):
        f = open("/sys/class/net/eth0/address","r")
        game_id = f.readline().strip()
        f.close()
        f = open(".gameid","w")
        f.write(game_id)
        f.close()
        return game_id

    mac = [
        random.randint(0x00, 0x7f),
        random.randint(0x00, 0x7f),
        random.randint(0x00, 0x7f),
        random.randint(0x00, 0xff),
        random.randint(0x00, 0xff),
        random.randint(0x00, 0xff) ]
    game_id = ":".join(map(lambda x: "%02x" % x, mac))

    f = open(".gameid","w")
    f.write(game_id)
    f.close()

    return game_id

def get_current_player():
    if not os.path.exists(".playername"):
        return "";

    f = open(".playername","r")
    n = f.readline().strip()
    f.close()
    return n

def get_game_name():
    global game_name
    return game_name

def convert(inp_data):
    if isinstance(inp_data, dict):
        return dict([(convert(key), convert(value)) for key, value in inp_data.iteritems()])
    elif isinstance(inp_data, list):
        return [convert(element) for element in inp_data]
    elif isinstance(inp_data, unicode):
        return inp_data.encode('utf-8')
    else:
        return inp_data

def post_score(score):
    global API_HOST
    #gameid, current_game, player, score
    try:
        gameid = get_game_id()
        player = get_current_player()
        game = get_game_name()

        print "Posting score " + str(score) + " to main server for " + player + " on " + game

        if player == "": return

        postvars = {"gameid": gameid, "current_game": game, "player": player, "score": score}
        data = urllib.urlencode(postvars)

        req = urllib2.Request(API_HOST+"post_score", data)
        rsp = urllib2.urlopen(req)
        content = rsp.read()
        print "post_score(): Server replied " + content
    except:
        pass

def get_scores():
    global last_score_pulldown_time, divisions, API_HOST
    try:
        if time.time() - last_score_pulldown_time >= 60:
            last_score_pulldown_time = time.time()
            postvars = {"gameid": get_game_id()}
            data = urllib.urlencode(postvars)
            req = urllib2.Request(API_HOST+"get_scores", data)
            rsp = urllib2.urlopen(req)
            divisions = convert(json.loads(rsp.read()))
        else:
            print "Not time to get scores yet. Its only been " + str(time.time() - last_score_pulldown_time) + "s"
    except:
        print "Could not grab scores from " + API_HOST + "get_scores"
        traceback.print_exc()

def get_player_picture(playerId):
    div = get_player_division(playerId)
    player = get_player(playerId)
    if div == None or player["staff"] == True:
        picture_file = "pb.i64"
    else:
        if div.lower() == "revenge from mars":
            picture_file = "rfm.i64"
        else:
            picture_file = "swe1.i64"

    return picture_file

def assemble_packet(packet_header, payload = None, override_size = None):
    # 1-2  packet header
    # 3-4  sequence
    # 5-8  packet size
    def_size = 8
    seq = get_next_seq_num()
    if payload != None:
        def_size += len(payload)
    packet = bytearray()
    packet.extend(packet_header)
    #for byte in seq:
    #    packet.append(byte)
    packet.extend(seq)

    if override_size != None:
        def_size = override_size

    sizeArr = int_to_bytes(def_size)
    #print sizeArr
    #for byte in sizeArr:
    #    packet.append(byte)
    packet.extend(sizeArr)

    if payload != None:
        packet.extend(payload)
    #    for byte in payload:
    #        packet.append(byte)
    #
    return packet

def assemble_reply(request, packet_header, payload = None, override_size = None):
    packet = assemble_packet(packet_header, payload, override_size)
    # JTS requires a response to echo the request's 16-bit transaction ID.
    # The original relay incorrectly generated an unrelated server counter,
    # so the game received the datagram but rejected it as stale/foreign.
    packet[2:4] = request[2:4]
    return packet

def send_message(packet, recipient):
    global sock

    outStr = ""
    for byte in packet:
        outStr += hex(byte) + " "

    #print "OUT: " + outStr

    sock.sendto(packet, recipient)

def process_message(sender, buf):
    global sock, server_packet_num, divisions, tournament_name, current_player_name, game_name
    arr = bytearray(buf)
    if len(arr) < 3: return
    indata = buf_to_string(buf)
    print "IN:  " + indata
    if arr[0] == 0x00:
        if arr[1] == 0x01:
            print "Got game over handshake..."
            division = bytes_to_string(arr[8:41])
            score = bytes_to_int(arr[48:52])

            print "User submitted score " + str(score) + " in division " + division

            # We automatically send a non-qualify message so we bypass the badging system
            # but record the score anyway
            #s.sendto(buffer(qualified_no), sender)
            response = assemble_reply(arr, pkt_resp_qualified, [0x00])
            send_message(response, sender)

            t = threading.Thread(target=post_score, args=(score,))
            t.start()

        if arr[1] == 0x03:
            print "Sending main information"
            #s.sendto(buffer(bytearray(main_info)), sender)
            #32b name
            #number of divisions 2b
            payload = copy_array_padded(string_to_bytes(tournament_name),32)
            payload.extend([0x00, 0x02])
            response = assemble_reply(arr, pkt_resp_main_info, payload)
            send_message(response, sender)

        if arr[1] == 0x04:
            print "Got request for division information " + hex(arr[9])
            divNum = arr[9]
            div = get_division(divNum)

            if div == None:
                print "DIVISION NOT FOUND"
                return

            divInfo = copy_array_padded(string_to_bytes(div["name"]),32)
            divInfo.extend([0x00,len(div["players"])])
            response = assemble_reply(arr, pkt_resp_div_info, divInfo)
            send_message(response, sender)

        if arr[1] == 0x05:
            print "Got request for division players"

            divNum = arr[9]
            div = get_division(divNum)

            pids = [0x0,len(div["players"])] # number of players
            pids.extend([0x00,0x00]) # Always this
            for player in div["players"]:
                pids.extend(int_to_bytes(player["id"]))
            # Append the IDs of all the players

            response = assemble_reply(arr, pkt_resp_div_players, pids)

            send_message(response, sender)

        if arr[1] == 0x06:
            # THIS IS AN UNKNOWN PACKET
            playerId = bytes_to_int(arr[8:13])
            print "User qualify check on ID " + str(playerId)

            # We automatically say no
            response = assemble_reply(arr, pkt_resp_qualified, [0x00])
            send_message(response, sender)

        if arr[1] == 0x07:
            playerId = bytes_to_int(arr[8:13])
            print "Picture request for player " + str(playerId)

            picture_file = get_player_picture(playerId)

            print "Picture file " + picture_file

            pic_size = os.path.getsize(picture_file)
            with open(picture_file, "rb") as f:
                chunk = f.read(1016)
                if chunk:
                    response = assemble_reply(arr, pkt_resp_picture, chunk,
                                              pic_size + 8)
                    send_message(response, sender)
                chunk = f.read(1024)
                time.sleep(0.1)
                while chunk:

                    sock.sendto(chunk, sender)

                    chunk = f.read(1024)
                    time.sleep(0.1)

        if arr[1] == 0x08:
            playerId = bytes_to_int(arr[8:13])
            print "Got player information request for player " + str(playerId)
            """
            ff f8 1a 6a 00 00 00 68 - header
            4a 69 6d 62 6f 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 - first name
            41 73 6b 65 79 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  - surname
            00 00 00 08 0d fb 35 9f - score
            00 00 0b 71 - picture size
            00 00 00 64
            00 00 00 32
            00 00 00 16
            00 00 01 80
            00 01 - position in ranking
            00 00


            0xff 0xf8 0x1a 0x6a 0x0 0x0 0x0 0x68
            0x4a 0x69 0x6d 0x62 0x6f 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0
            0x41 0x73 0x6b 0x65 0x79 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0
            0x0 0x0 0x0 0x8 0xd 0xfb 0x35 0x9f
            0x0 0x0 0xa 0xad
            0x0 0x0 0x0 0x64
            0x0 0x0 0x0 0x32
            0x0 0x0 0x0 0x16
            0x0 0x0 0x1 0x80
            0x0 0x1
            0x0 0x0
            """

            player = get_player(playerId)

            firstname = copy_array_padded(string_to_bytes(player["name"]), 32)
            lastname = copy_array_padded(string_to_bytes(" "), 32)
            score = int_to_bytes8(player["score"])
            picture_size = int_to_bytes(os.path.getsize(get_player_picture(playerId)))
            position = [0x00, player["rank"]]

            payload = firstname
            payload.extend(lastname)
            payload.extend(score)
            payload.extend(picture_size)
            payload.extend([0x0,0x0,0x0,0x64])
            payload.extend([0x0,0x0,0x0,0x32])
            payload.extend([0x0,0x0,0x0,0x16])
            payload.extend([0x0,0x0,0x1,0x80])
            payload.extend(position)
            payload.extend([0,0])
            response = assemble_reply(arr, pkt_resp_player_info, payload)

            send_message(response, sender)

        if arr[1] == 0x09:
            print "Power on message"
            #sock.sendto(buffer(bytearray(self_test)), sender)
            response = assemble_reply(arr, pkt_resp_power_on)
            send_message(response, sender)

        if arr[1] == 0x14:
            player_name = bytes_to_string(arr[8:41])
            print "Player " + player_name + " registered"
            f = open(".playername", "w")
            f.write(player_name)
            f.close()

            current_player_name = player_name

        if arr[1] == 0x15:
            gn = bytes_to_string(arr[8:37])
            print "Game is " + gn

            if gn[0:9].lower() == "star wars":
                game_name = "SWE1"
            else:
                game_name = "RFM"

            t = threading.Thread(target=get_scores)
            t.start()


def main():
    global server_packet_num, sock
    time.sleep(5)
    t = threading.Thread(target=get_scores)
    t.start()
    server_packet_num = 0
    print "=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-"
    print "                 Pinbox Tournament Server"
    print "                  http://playpinbox.com"
    print "=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-"
    print "Game ID " + get_game_id()
    print "Starting listener on " + str(SERVER_PORT)

    s = socket(AF_INET, SOCK_DGRAM)

    s.bind(('', SERVER_PORT))
    sock = s
    print "Listener started. Receiving data..."

    while 1:
        data, addr = s.recvfrom(BUFSIZE)
        #s.sendto(data, addr)
        process_message(addr, data)

main()
