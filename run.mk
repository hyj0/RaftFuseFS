clean_and_kill:kill_Server clear_log
	echo ""

clear_log:
	sudo rm ./raft_data*/* -rv || echo ""

kill_Server:
	sudo killall Server || echo ""
	sudo umount /tmp/FuseTarget || echo ""
	sudo umount /tmp/FuseTarget1 || echo ""
run0:
	sudo ./cmake-build-debug/Server  -data_path=`pwd`/raft_data   -fuse_args='-f  -o umask=0002 -o uid=1003 -o gid=1003 -o allow_other /tmp/FuseTarget' -src_dir=/tmp/FuseSrc/
run1:
	sudo ./cmake-build-debug/Server -port=8101  -conf=`hostname -i`:8100:0 -data_path=`pwd`/raft_data1   -fuse_args='-f -o umask=0002 -o uid=1003 -o gid=1003 -o allow_other /tmp/FuseTarget1' -src_dir=/tmp/FuseSrc1/

join1:
	./cmake-build-debug/thirth_party/braft/tools/output/bin/braft_cli add_peer --group=fuse --conf=`hostname -i`:8100:0  --peer=`hostname -i`:8101:0
deug0:
	sudo gdbserver :8889 ./cmake-build-debug/Server  -data_path=`pwd`/raft_data   -fuse_args='-d  -o umask=0002 -o uid=1003 -o gid=1003 -o allow_other /tmp/FuseTarget' -src_dir=/tmp/FuseSrc/
diff:
	sudo diff -urNa /tmp/FuseSrc /tmp/FuseSrc1/