#!/bin/bash

echo "Запуск контейнеров"
docker-compose up -d

echo "Имитация потери пакетов(30%)"
docker-compose exec worker1 iptables -A INPUT -m statistic --mode random --probability 0.30 -j DROP
docker-compose exec worker2 iptables -A INPUT -m statistic --mode random --probability 0.30 -j DROP
docker-compose exec worker3 iptables -A INPUT -m statistic --mode random --probability 0.30 -j DROP
docker-compose exec worker4 iptables -A INPUT -m statistic --mode random --probability 0.30 -j DROP
sleep 100

echo "Логи"
docker-compose logs

docker-compose down