#!/usr/bin/env bash
PI=${PI:-pi}
DIR=mini-online

case "$1" in
  status)  ssh "$PI" "cd $DIR && docker compose ps" ;;
  logs)    ssh "$PI" "cd $DIR && docker compose logs --tail ${2:-30} gameserver matchmaker" ;;
  follow)  ssh -t "$PI" "cd $DIR && docker compose logs -f ${2:-gameserver}" ;;
  stats)   ssh "$PI" "docker stats --no-stream --format 'table {{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}'" ;;
  logsize) ssh "$PI" "sudo sh -c 'du -sh /var/lib/docker/containers/*/*-json.log'" ;;
  restart) ssh "$PI" "cd $DIR && docker compose restart ${2:-matchmaker}" ;;
  up)      ssh "$PI" "cd $DIR && docker compose up -d" ;;
  down)    ssh "$PI" "cd $DIR && docker compose down" ;;
  udperr)  ssh "$PI" "cd $DIR && docker compose exec -T gameserver cat /proc/net/snmp" | awk '/^Udp:/{ if(!h){ for(i=1;i<=NF;i++) k[i]=$i; h=1 } else { for(i=2;i<=NF;i++) if(k[i]=="InDatagrams"||k[i]=="InErrors"||k[i]=="RcvbufErrors") printf "%s=%s ", k[i], $i; print "" } }' ;;
  capture) SECS=${2:-10}
           ssh "$PI" "sudo timeout $SECS tcpdump -i eth0 -w /tmp/capture.pcap udp port 9000; sudo chmod a+r /tmp/capture.pcap"
           scp "$PI:/tmp/capture.pcap" ./capture.pcap && echo "saved ./capture.pcap" ;;
  *)       echo "usage: ./pitool.sh status|logs|follow|stats|logsize|restart|up|down|capture [arg]" ;;
esac