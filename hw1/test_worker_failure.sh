#!/bin/bash

echo "Запуск контейнеров"
docker-compose up -d

echo "Падения воркера"
docker-compose stop worker2
sleep 10
echo "Восстановление воркера"
docker-compose start worker2
sleep 30

echo "Логи"
docker-compose logs

docker-compose down 