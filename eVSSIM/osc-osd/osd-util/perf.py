#!/usr/bin/python
#
# Start and stop everything required to do PVFS, subject to lots of options
# about OSD, protocol, storage, etc.
#
# Relies on pvfs2 commands being in the path already.
#
# Copyright (C) 2007-8 Pete Wyckoff <pw@osc.edu>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import os
import errno
import sys
import pwd
import csv
import socket

# defaults
options = {
    "protocol": "auto",
    "osdtype": "none",
    "dirtype": "pvfs",
    "storage": "disk",
    "meta_on_io": "no",
    "one_config_file" : "yes",
    "mirror": "",
    "pvfs_osd_integrated": "no",
    "rdma": "no",
}

# hostname
host = socket.gethostname()
# locations of external codes
id = pwd.getpwuid(os.getuid())[0]

if id == "pw":
    osd_dir = "/home/pw/src/osd"
elif id == "dennis":
    osd_dir = "/home/dennis/Projects/OSD/osd"
elif id == "alin":
    osd_dir = "/home/alin/research/osd"
elif id == "ananth":
    osd_dir = "/home/ananth/osd"
else:
    osd_dir = os.environ["HOME"] + "/osd"

initiator = osd_dir + "/osd-util/initiator"
tgtd      = osd_dir + "/stgt/tgtd"
pvfs_init = osd_dir + "/osd-initiator/python/pvfs-init.py"
pvfs_osd_integrated_init = osd_dir + "/osd-initiator/python/pvfs-osd-integrated-init.py"
allify_code = "/home/pw/bin/allify"

# old location in target dir
if not os.access(tgtd, os.X_OK):
    tgtd = osd_dir + "/osd-target/tgtd"

#
# All the possibilities for -o and -d.
# N = numion is always the number of IO servers, even if some of
# those IO servers also do other things: config or dir or md service.
#
# 1 = config = responsibility for FS configuration
# D = dir = all other directories
# M = md = metafiles
# N = io = datafiles
#
# -o none -d pvfs (the default)
#   M pvfs dir+md server, N pvfs io servers, D := M
# -o none -d pvfs -mio
#   M pvfs dir+md+io server, N-M pvfs io servers, D := M, N >= M
#
# (-mio not valid when not (-o none))
# -o datafile -d pvfs
#   M pvfs dir+md server, N osd io servers, D := M
# -o metafile -d pvfs
#   D pvfs dir server, M osd md server, N osd io servers
# -o mdfile -d pvfs
#   D pvfs dir server, N osd md+io servers, M := N
#
# -o none -d attr4|attr1|obj
#   M pvfs md server, D osd dir server, N pvfs io servers
# -o none -d attr4|attr1|obj -mio
#   M pvfs md+io server, D osd dir server, N-M pvfs io servers, N >= M
#
# -o datafile -d attr4|attr1|obj
#   M pvfs md server, D osd dir server, N osd io servers
# -o metafile -d attr4|attr1|obj
#   0 pvfs server, M osd dir+md server, N osd io servers, D := M
# -o mdfile -d attr4|attr1|obj
#   0 pvfs server, N osd dir+md+io servers, M := N, D := M

# Default number of dedicated nodes for these things.  Sometimes
# the functions are packed together, depending on the options.
# Note that numion always comes from the parameter to start.
# It is safe to ask for more than 1 nummeta or numdir here, if
# interested to see that sort of scaling, but cannot choose different
# values for meta vs dir or meta vs io in some cases.  It goes max.
# Never makes sense to have numconfig > 1, but left here as symbolic aid.
numion = -1
nummeta = 1
numdir = 1
numconfig = 1

def usage():
    print >>sys.stderr, "Usage:", sys.argv[0], "[Options] start <numion>"
    print >>sys.stderr, "      ", sys.argv[0], "[Options] restart [<numion>]"
    print >>sys.stderr, "      ", sys.argv[0], "stop"
    print >>sys.stderr, "      ", sys.argv[0], "status"
    print >>sys.stderr, "  -p {protocol tcp|ib|portals}, default", \
    		        options["protocol"]
    print >>sys.stderr, "  -o {osdtype none|datafile|metafile|mdfile},", \
    			"default", options["osdtype"]
    print >>sys.stderr, "  -d {dirtype pvfs|attr4|attr1|obj},", \
    			"default", options["dirtype"]
    print >>sys.stderr, "  -s {storage disk|tmpfs}, default", options["storage"]
    print >>sys.stderr, "  -m <nummeta>, default 1"
    print >>sys.stderr, "  -mio : metadata servers on IO servers (-o none", \
    			"only), not default"
    print >>sys.stderr, "  -2 : two config files (ancient PVFS), not default"
    print >>sys.stderr, "  -remote-mount <metanodes file> <ionodes file>"
    print >>sys.stderr, "  -meta-mirror <ionodes file>"
    print >>sys.stderr, "  -data-cache"
    print >>sys.stderr, "  -poi : for PVFS_OSD_INTEGRATED, connect to OSDs"
    print >>sys.stderr, "  -rdma : connect to OSDs using iSER"
    sys.exit(1)

# filenames
testdir = os.environ["TMPDIR"]
foptions = testdir + "/options"
fnodes = testdir + "/nodes"
fcompnodes = testdir + "/compnodes"
fpvfsnodes = testdir + "/pvfsnodes"
fosdnodes = testdir + "/osdnodes"
fmetanodes = testdir + "/metanodes"
fionodes = testdir + "/ionodes"

tabfile = testdir + "/pvfs2tab"
fsconf = testdir + "/fs.conf"
serverconf = testdir + "/server.conf"

# possible locations of remote nodes for mirror config
mirror_metanodes = ""
mirror_ionodes = ""

def allbut(nodes, numservers):
    # keep 1 extra as a compute node
    if numservers > len(nodes):
	print >>sys.stderr, "Too many server nodes for PBS request."
	sys.exit(1)
    if numservers == len(nodes):
	print >>sys.stderr, "Warning: no nodes left to be clients."

    # these are the compnodes, high nodes will be pvfs or osd servers
    return nodes[0:-numservers]


def handle_alloc():
    global options, osdnodes, pvfsnodes, metanodes, ionodes
    global datahandles, metahandles, roothandle, roothandle_node

    # Keep handle ranges >= 0x10000 to avoid OSD reserved OID space
    # and use pretty numbers for easier debugging rather than worrying
    # about filling the space.
    h = 1000000
    datastep = 1000000
    metastep = 1000000
    roothandle = 0

    # When caching, make sure handle range on cache side can accommodate
    # all handles at the data side.  When they have different number of
    # servers, for example, this is an issue.  Hope everything divides.
    if options["mirror"] == "data-cache":
	fd = open(mirror_ionodes)
	num = 0
	while True:
	    line = fd.readline()
	    if line == "":
		break
	    num += 1
	fd.close()
	datastep = num * datastep / len(ionodes)

    datahandles = {}
    for n in ionodes:
	datahandles[n] = (h, h + datastep-1)
	h += datastep

    # if directories are being handled by OSDs, put the root handle there
    if len(metanodes) == 0:
	print >>sys.stderr, "No metanodes to alloc."
	sys.exit(1)

    if options["mirror"] == "meta-mirror" or \
       options["mirror"] == "data-cache":
	fd = open(mirror_metanodes)
	num = 0
	while True:
	    line = fd.readline()
	    if line == "":
		break
	    num += 1
	fd.close()
	metastep = num * metastep / len(metanodes)

    metahandles = {}
    for n in metanodes:
	metahandles[n] = (h, h + metastep-1)
	if roothandle == 0:
	    if (options["dirtype"] == "pvfs" and n in pvfsnodes) or \
	       (options["dirtype"] != "pvfs" and n in osdnodes):
		roothandle = h
		roothandle_node = n
	h += metastep


def buildfiles():
    global numion, nummeta, numdir, options
    global nodes, compnodes, osdnodes, pvfsnodes, metanodes, ionodes

    # look at executable to figure out protocol, if auto
    if options["protocol"] == "auto":
	options["protocol"] = "tcp"
	ret = os.system("ldd $(type -p pvfs2-ls) 2>/dev/null |" \
			+ " egrep -q '(libvapi|libibverbs)'")
	if ret == 0:
	    options["protocol"] = "ib"
	else:
	    ret = os.system("nm $(type -p pvfs2-ls) 2>/dev/null |" \
	    		    + " egrep -qw PtlInit")
	    if ret == 0:
		options["protocol"] = "portals"

    # pick port and mountpoint based on protocol
    port = 3334
    if options["protocol"] == "ib":
	port = 3335
    mountpoint = "/pvfs"
    if options["protocol"] != "tcp":
	mountpoint = mountpoint + "-" + options["protocol"]

    # list of all nodes from PBS
    nodes = []
    fd = os.popen("uniq $PBS_NODEFILE")
    while True:
	line = fd.readline()
	if line == "":
	    break
	nodes.append(line[:-1])
    fd.close()

    if options["meta_on_io"] == "yes":
	if options["osdtype"] != "none":
	    print >>sys.stderr, "Option -mio only works in case \"-o none\"."
	    sys.exit(1)
	if options["mirror"] != "":
	    print >>sys.stderr, "Option -mio is not for use with \"-mirror\"."
	    sys.exit(1)

    # figure out how many nodes are needed for servers, rest will be clients
    if options["dirtype"] == "pvfs":
	if options["osdtype"] == "none":
	    if options["meta_on_io"] == "no":
		numdir = nummeta
		compnodes = allbut(nodes, numion + nummeta)
		osdnodes = []
		pvfsnodes = nodes[len(compnodes):]
		# they are disjoint
		ionodes = pvfsnodes[:numion]
		metanodes = pvfsnodes[numion:]
	    else:
		numdir = nummeta
		if nummeta > numion:
		    print >>sys.stderr, "Not enough ion for requested meta."
		    sys.exit(1)
		compnodes = allbut(nodes, numion)
		osdnodes = []
		pvfsnodes = nodes[len(compnodes):]
		# pack them together
		ionodes = pvfsnodes[:numion]
		metanodes = pvfsnodes[:nummeta]
	elif options["osdtype"] == "datafile":
	    numdir = nummeta
	    compnodes = allbut(nodes, numion + nummeta)
	    osdnodes = nodes[len(compnodes):len(compnodes)+numion]
	    pvfsnodes = nodes[len(compnodes)+len(osdnodes):]
	    ionodes = osdnodes
	    metanodes = pvfsnodes
	elif options["osdtype"] == "metafile":
	    compnodes = allbut(nodes, numion + nummeta + numdir)
	    osdnodes = nodes[len(compnodes):len(compnodes)+numion+nummeta]
	    pvfsnodes = nodes[len(compnodes)+len(osdnodes):]
	    ionodes = osdnodes[:numion]
	    metanodes = osdnodes[numion:] + pvfsnodes
	elif options["osdtype"] == "mdfile":
	    nummeta = numion
	    compnodes = allbut(nodes, numion + numdir)
	    osdnodes = nodes[len(compnodes):len(compnodes)+numion]
	    pvfsnodes = nodes[len(compnodes)+len(osdnodes):]
	    ionodes = osdnodes
	    metanodes = pvfsnodes
	else:
	    print >>sys.stderr, "Unknown osdtype", options["osdtype"]
	    sys.exit(1)
    elif options["dirtype"] == "attr4" \
      or options["dirtype"] == "attr1" \
      or options["dirtype"] == "obj":
	if options["osdtype"] == "none":
	    if options["meta_on_io"] == "no":
		compnodes = allbut(nodes, numion + nummeta + numdir)
		osdnodes = nodes[len(compnodes):len(compnodes)+numdir]
		pvfsnodes = nodes[len(compnodes)+len(osdnodes):]
		# disjoint
		ionodes = pvfsnodes[nummeta:]
		metanodes = osdnodes + pvfsnodes[0:nummeta]
	    else:
		if nummeta > numion:
		    print >>sys.stderr, "Not enough ion for requested meta."
		    sys.exit(1)
		compnodes = allbut(nodes, numion + numdir)
		osdnodes = nodes[len(compnodes):len(compnodes)+numdir]
		pvfsnodes = nodes[len(compnodes)+len(osdnodes):]
		# pack
		ionodes = pvfsnodes
		metanodes = osdnodes + pvfsnodes[0:nummeta]
	elif options["osdtype"] == "datafile":
	    compnodes = allbut(nodes, numion + nummeta + numdir)
	    osdnodes = nodes[len(compnodes):len(compnodes)+numion+numdir]
	    pvfsnodes = nodes[len(compnodes)+len(osdnodes):]
	    ionodes = osdnodes
	    metanodes = osdnodes[numion:] + pvfsnodes
	elif options["osdtype"] == "metafile":
	    numdir = nummeta
	    compnodes = allbut(nodes, numion + nummeta)
	    osdnodes = nodes[len(compnodes):len(compnodes)+numion+nummeta]
	    pvfsnodes = []
	    ionodes = osdnodes[:numion]
	    metanodes = osdnodes[numion:]
	elif options["osdtype"] == "mdfile":
	    nummeta = numion
	    numdir = nummeta
	    compnodes = allbut(nodes, numion)
	    osdnodes = nodes[len(compnodes):len(compnodes)+numion]
	    pvfsnodes = []
	    ionodes = osdnodes
	    metanodes = osdnodes
	else:
	    print >>sys.stderr, "Unknown osdtype", options["osdtype"]
	    sys.exit(1)
    else:
	print >>sys.stderr, "Unknown dirtype", options["dirtype"]
	sys.exit(1)

    # Override the above, possibly, using given list(s) of ionodes, metanodes.
    if options["mirror"] == "remote-mount" or \
       options["mirror"] == "meta-mirror":
	# replace ionodes
	for x in ionodes:
	    pvfsnodes.remove(x)
	    compnodes.append(x)
	ionodes = []
	fd = open(mirror_ionodes)
	while True:
	    line = fd.readline()
	    if line == "":
		break
	    line = line[:-1]
	    ionodes.append(line)
	    pvfsnodes.append(line)
	fd.close()
    if options["mirror"] == "remote-mount":
	# replace metanodes
	for x in metanodes:
	    pvfsnodes.remove(x)
	    compnodes.append(x)
	metanodes = []
	fd = open(mirror_metanodes)
	while True:
	    line = fd.readline()
	    if line == "":
		break
	    line = line[:-1]
	    metanodes.append(line)
	    pvfsnodes.append(line)
	fd.close()

    # figure out the meta and data handle ranges
    global roothandle, roothandle_node
    handle_alloc()

    # store the options for status reporting later
    fd = open(foptions, "w")
    writer = csv.writer(fd, dialect='excel-tab')
    map(writer.writerow, options.items())
    fd.close()

    # store all node lists in files
    for a in [[nodes, fnodes],
              [compnodes, fcompnodes], [osdnodes, fosdnodes],
	      [pvfsnodes, fpvfsnodes], [ionodes, fionodes],
	      [metanodes, fmetanodes]]:
	fd = open(a[1], "w")
	for i in a[0]:
	    print >>fd, i
	fd.close()


    # write fs.conf
    fd = open(fsconf, "w")
    print >>fd, "<Defaults>"
    print >>fd, "    UnexpectedRequests 50"
    print >>fd, "    EventLogging none"
    print >>fd, "    LogStamp usec"
    print >>fd, "    BMIModules bmi_" + options["protocol"]
    print >>fd, "    FlowModules flowproto_multiqueue"
    print >>fd, "    PerfUpdateInterval 1000"
    print >>fd, "    ServerJobBMITimeoutSecs 30"
    print >>fd, "    ServerJobFlowTimeoutSecs 30"
    print >>fd, "    ClientJobBMITimeoutSecs 300"
    print >>fd, "    ClientJobFlowTimeoutSecs 300"
    print >>fd, "    ClientRetryLimit 5"
    print >>fd, "    ClientRetryDelayMilliSecs 2000"
    if options["osdtype"] != "none":
	print >>fd, "    OSDType " + options["osdtype"]
    if options["dirtype"] != "pvfs":
	print >>fd, "    OSDDirType " + options["dirtype"]
    if options["one_config_file"] == "yes":
	print >>fd, "    StorageSpace", testdir + "/storage"
	print >>fd, "    LogFile", testdir + "/pvfs2.log"
    print >>fd, "</Defaults>"

    print >>fd
    print >>fd, "<Aliases>"
    for n in osdnodes:
	print >>fd, "    Alias", n, "osd://" + n
    for n in pvfsnodes:
	print >>fd, "    Alias", n, options["protocol"] + "://" + n + ":" + \
	            str(port)
    print >>fd, "</Aliases>"

    global pid
    pid = 424242
    print >>fd
    print >>fd, "<Filesystem>"
    print >>fd, "    Name pvfs2-fs"
    print >>fd, "    ID", pid
    if options["mirror"] != "":
        print >>fd, "    IsMirror", 1

    print >>fd, "    <DataHandleRanges>"
    for n in ionodes:
	print >>fd, "        Range", n, "%d-%d" % \
		    (datahandles[n][0], datahandles[n][1])
    print >>fd, "    </DataHandleRanges>"
    print >>fd, "    <MetaHandleRanges>"
    for n in metanodes:
	print >>fd, "        Range", n, "%d-%d" % \
		    (metahandles[n][0], metahandles[n][1])
    print >>fd, "    </MetaHandleRanges>"

    print >>fd, "    RootHandle %d" % roothandle
    print >>fd, "    <StorageHints>"
    print >>fd, "        TroveSyncMeta no"
    print >>fd, "        TroveSyncData no"
    print >>fd, "        ImmediateCompletion yes"
    print >>fd, "        CoalescingHighWatermark infinity"
    print >>fd, "        CoalescingLowWatermark 1"
    print >>fd, "        TroveMethod dbpf"
    print >>fd, "    </StorageHints>"
    # default is simple_stripe if nothing set
#    # basic
#    print >>fd, "    <Distribution>"
#    print >>fd, "        Name basic_dist"
#    print >>fd, "    </Distribution>"
    # twod_stripe
#    print >>fd, "    <Distribution>"
#    print >>fd, "        Name twod_stripe"
#    print >>fd, "        Param strip_size"
#    print >>fd, "        Value 65536"
#    print >>fd, "        Param num_groups"
#    print >>fd, "        Value 2"
#    print >>fd, "        Param group_strip_factor"
#    print >>fd, "        Value 256"
#    print >>fd, "    </Distribution>"
    print >>fd, "    FlowBufferSizeBytes 16777216"
    print >>fd, "</Filesystem>"
    fd.close()

    # generate server.conf-* for pvfs server nodes
    if options["one_config_file"] == "no":
	for n in pvfsnodes:
	    fd = open(serverconf + "-" + n, "w")
	    print >>fd, "StorageSpace", testdir + "/storage"
	    print >>fd, "HostID \"%s://%s:%d\"" % (options["protocol"], n, port)
	    print >>fd, "LogFile", testdir + "/pvfs2.log"
	    fd.close()

    # generate pvfs2tab, pick any pvfs node as config server
    global tabfile_contents
    if roothandle_node in osdnodes:
	tabfile_contents = "osd://%s/pvfs2-fs %s pvfs2 defaults 0 0" \
	    % (roothandle_node, mountpoint)
    else:
	tabfile_contents = "%s://%s:%d/pvfs2-fs %s pvfs2 defaults 0 0" \
	    % (options["protocol"], roothandle_node, port, mountpoint)


def read_one_file(filename):
    l = []
    fd = open(filename)
    while True:
	line = fd.readline()
	if line == "":
	    break
	l.append(line[:-1])
    fd.close()
    return l


def readfiles():
    global options
    options = {}
    if not os.access(foptions, os.F_OK):
	print >>sys.stderr, "File", foptions, "does not exist."
	print >>sys.stderr, "Perhaps you did not do \"perf start\"."
	sys.exit(1)
    fd = open(foptions)
    reader = csv.reader(fd, dialect='excel-tab')
    while True:
	try:
	    row = reader.next()
	except StopIteration:
	    break
	options[row[0]] = row[1]
    fd.close()

    global nodes, compnodes, osdnodes, pvfsnodes, ionodes, metanodes
    nodes = read_one_file(fnodes)
    compnodes = read_one_file(fcompnodes)
    osdnodes = read_one_file(fosdnodes)
    pvfsnodes = read_one_file(fpvfsnodes)
    ionodes = read_one_file(fionodes)
    metanodes = read_one_file(fmetanodes)

    global numion, nummeta
    if numion == -1:
	numion = len(ionodes)
    if nummeta == -1:
	nummeta = len(metanodes)


def allify(n):
    (fdto, fdfrom) = os.popen2(allify_code)
    for i in n:
	print >>fdto, i
    fdto.close()
    s = fdfrom.read()
    fdfrom.close()
    s = s[:-1]
    return s


def start():
    # don't mess with certain classes of nodes if mirror
    mypvfsnodes = pvfsnodes

    if options["mirror"] == "remote-mount" or \
       options["mirror"] == "meta-mirror":
	mypvfsnodes = [x for x in mypvfsnodes if x not in ionodes]
    if options["mirror"] == "remote-mount":
	mypvfsnodes = [x for x in mypvfsnodes if x not in metanodes]

    # sync files to pvfs nodes
    for n in mypvfsnodes:
	if options["one_config_file"] == "yes":
	    os.system("rcp "
		       + testdir + "/fs.conf " + n + ":" + testdir)
	else:
	    os.system("rcp "
		       + testdir + "/{fs.conf,server.conf-" + n + "} "
		       + n + ":" + testdir)

    # start osd targets
    if len(osdnodes) > 0:
	if options["storage"] == "tmpfs":
	    mountup = "sudo mount -t tmpfs -o size=1800m none /tmp/tgt-" + id + " \; "
	else:
	    mountup = ""

	os.system("all -p " + allify(osdnodes) + " "
	    + "cd " + testdir + " \; "
	    + "rm -rf /tmp/tgt-" + id + " \; "
	    + "mkdir /tmp/tgt-" + id + " \; "
	    + mountup
	    + tgtd + " -d 9 \< /dev/null \&\> tgtd.log")

    if len(mypvfsnodes) > 0:
	if options["storage"] == "tmpfs":
	    mountup = "sudo mount -t tmpfs -o size=1800m none " + testdir + "/storage \;"
	else:
	    mountup = ""

	if options["one_config_file"] == "yes":
	    os.system("all -p " + allify(mypvfsnodes) + " "
		+ "cd " + testdir + " \; "
		+ "rm -rf " + testdir + "/storage \; "
		+ "mkdir " + testdir + "/storage \; "
		+ mountup
		+ "rm -f pvfs2.log \;"
		+ "pvfs2-server --mkfs fs.conf \;"
		+ "TZ=EST5EDT pvfs2-server fs.conf")
	else:
	    os.system("all -p " + allify(mypvfsnodes) + " "
		+ "cd " + testdir + " \; "
		+ "rm -rf " + testdir + "/storage \; "
		+ "mkdir " + testdir + "/storage \; "
		+ mountup
		+ "ln -fs server.conf-\$\(hostname\) server.conf \; "
		+ "rm -f pvfs2.log \;"
		+ "pvfs2-server --mkfs fs.conf server.conf \;"
		+ "TZ=EST5EDT pvfs2-server fs.conf server.conf")

    # compnodes
    myosdnodes = osdnodes
    if options["pvfs_osd_integrated"] == "yes":
	myosdnodes = osdnodes + mypvfsnodes

    # append "-ib" to the osdnodes if using rdma
    if options["rdma"] == "yes":
	startcmd = "start --rdma"
	if host.rfind("opt") == -1:
	    myibosdnodes = [ n + "-ib" for n in myosdnodes ]
	else:
	    myibosdnodes = [ n.replace('opt', 'opt-ib-') for n in myosdnodes ]
    else:
	myibosdnodes = myosdnodes
	startcmd = "start"

    if len(myibosdnodes) > 0:
	os.system("all " + allify(compnodes) + " "
	    + "echo " + tabfile_contents + " \> " + tabfile + " \; "
	    + "sudo " + initiator + " " + startcmd + " "
	    + " ".join(myibosdnodes))
    elif len(compnodes) > 0:
	os.system("all -p " + allify(compnodes) + " "
	    + "echo " + tabfile_contents + " \> " + tabfile)
    else:
	# leave it on this starting machine at least
    	os.system("echo " + tabfile_contents + " > " + tabfile)

    # format and initial pvfs layout on osd nodes.  The machine where
    # this script runs must be one of the compnodes so it can talk to
    # all the OSDs with iscsi.  (Or could rsh to one.)
    for n in osdnodes:
	if datahandles.has_key(n):
	    d = datahandles[n][0]
	else:
	    d = 0
	if metahandles.has_key(n):
	    m = metahandles[n][0]
	else:
	    m = 0
	if m == roothandle:
   	    s = " " + str(m) + " " + fsconf
	else:
	    s = " "
	#print pvfs_init + " " + n + " " + str(d) + " " + str(m) + s
	ret = os.system(pvfs_init + " " + n + " " + str(d) + " " + str(m) + s)
	if ret:
	    print >>sys.stderr, pvfs_init + " failed"

    # just format and create the partition on these
    if options["pvfs_osd_integrated"] == "yes":
	for n in mypvfsnodes:
	    ret = os.system(pvfs_osd_integrated_init + " " + n + " " + str(pid))
	    if ret:
		print >>sys.stderr, pvfs_osd_integrated_init + " failed"

def status():
    print "Protocol:               ", options["protocol"]
    print "OSDType:                ", options["osdtype"]
    print "OSDDirType:             ", options["dirtype"]
    print "Storage:                ", options["storage"]
    print "Meta-on-io:             ", options["meta_on_io"]
    print "One config file:        ", options["one_config_file"]
    print "Mirror:                 ", options["mirror"]

    n = filter(lambda x: x in metanodes, osdnodes)
    if len(n) > 0:
	print "Metadata servers (osd): ", " ".join(n)

    n = filter(lambda x: x in metanodes, pvfsnodes)
    if len(n) > 0:
	print "Metadata servers (pvfs):", " ".join(n)

    n = filter(lambda x: x in ionodes, osdnodes)
    if len(n) > 0:
	print "IO servers (osd):       ", " ".join(n)

    n = filter(lambda x: x in ionodes, pvfsnodes)
    if len(n) > 0:
	print "IO servers (pvfs):      ", " ".join(n)

    if True:
	print "Clients:                ", " ".join(compnodes)

    print "export PVFS2TAB_FILE=" + tabfile


def stop():
    # don't mess with io nodes if just a mirror
    mypvfsnodes = pvfsnodes
    if options["mirror"] != "":
	mypvfsnodes = [x for x in pvfsnodes if x not in ionodes]

    # compnodes
    if len(osdnodes) > 0:
	os.system("all -p " + allify(compnodes) + " "
	    + "sudo " + initiator + " stop \; "
	    + "rm " + tabfile)
    elif len(compnodes) > 0:
	os.system("all -p " + allify(compnodes) + " "
	    + "rm " + tabfile)
    else:
	os.system("rm " + tabfile)

    if len(osdnodes) > 0:
	os.system("all -p " + allify(osdnodes) + " "
	    + "cd " + testdir + " \; "
	    + "killall -9 tgtd \; "
	    + "sleep 1 \; "
	    + "sudo umount /tmp/tgt-" + id + " 2>/dev/null \; "
	    + "rm -f tgtd.log")
    if len(mypvfsnodes) > 0:
	os.system("all -p " + allify(mypvfsnodes) + " "
	    + "cd " + testdir + " \; "
	    + "killall -9 pvfs2-server \; "
	    + "sleep 1 \; "
	    + "sudo umount " + testdir + "/storage 2>/dev/null \; "
	    + "rm -rf pvfs2.log fs.conf server.conf server.conf-\* storage")

    for f in [fnodes, fcompnodes, fpvfsnodes, fosdnodes, fionodes, fmetanodes,
    	      fsconf, foptions]:
	try:
	    os.unlink(f)
	except OSError,e:
	    if e[0] == errno.ENOENT and f == fsconf and \
	       numion + nummeta == len(nodes):
		# no compute nodes, data side of mirror case
		pass
	    else:
		raise e
    if not options["one_config_file"] == "yes":
	for n in mypvfsnodes:
	    os.unlink(serverconf + "-" + n)


i = 1
while i < len(sys.argv):
    if sys.argv[i] == "-p":
	if i+1 == len(sys.argv):
	    usage()
	options["protocol"] = sys.argv[i+1]
	i += 2
    elif sys.argv[i] == "-o":
	if i+1 == len(sys.argv):
	    usage()
	options["osdtype"] = sys.argv[i+1]
	i += 2
    elif sys.argv[i] == "-d":
	if i+1 == len(sys.argv):
	    usage()
	options["dirtype"] = sys.argv[i+1]
	i += 2
    elif sys.argv[i] == "-s":
	if i+1 == len(sys.argv):
	    usage()
	options["storage"] = sys.argv[i+1]
	i += 2
    elif sys.argv[i] == "-m":
	if i+1 == len(sys.argv):
	    usage()
	nummeta = int(sys.argv[i+1])
	i += 2
    elif sys.argv[i] == "-mio":
	options["meta_on_io"] = "yes"
	i += 1
    elif sys.argv[i] == "-2":
	options["one_config_file"] = "no"
	i += 1
    elif sys.argv[i] == "-remote-mount":
	if i+2 >= len(sys.argv):
	    usage()
	mirror_metanodes = sys.argv[i+1]
	mirror_ionodes = sys.argv[i+2]
	options["mirror"] = sys.argv[i][1:]
	i += 3
    elif sys.argv[i] == "-meta-mirror":
	if i+2 >= len(sys.argv):
	    usage()
	mirror_metanodes = sys.argv[i+1]
	mirror_ionodes = sys.argv[i+2]
	options["mirror"] = sys.argv[i][1:]
	i += 3
    elif sys.argv[i] == "-data-cache":
	if i+2 >= len(sys.argv):
	    usage()
	mirror_metanodes = sys.argv[i+1]
	mirror_ionodes = sys.argv[i+2]
	options["mirror"] = sys.argv[i][1:]
	i += 3
    elif sys.argv[i] == "-poi":
	options["pvfs_osd_integrated"] = "yes"
	i += 1
    elif sys.argv[i] == "-rdma":
	options["rdma"] = "yes"
	i += 1
    else:
	break

if i == len(sys.argv):
    usage()
if sys.argv[i] == "start":
    if len(sys.argv) != i+2:
	usage()
    numion = int(sys.argv[i+1])
    buildfiles()
    start()
    status()
elif sys.argv[i] == "restart":
    if len(sys.argv) == i+1:
	numion = -1  # get from readfiles
	nummeta = -1
    elif len(sys.argv) == i+2:
	numion = int(sys.argv[i+1])
    else:
	usage()
    readfiles()
    stop()
    buildfiles()
    start()
    status()
elif sys.argv[i] == "stop":
    if len(sys.argv) != i+1:
	usage()
    readfiles()
    stop()
elif sys.argv[i] == "status":
    if len(sys.argv) != i+1:
	usage()
    readfiles()
    status()
else:
    usage()

