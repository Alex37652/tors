#!/bin/bash

echo "Запуск контейнеров"
docker-compose up -d

echo "Падения воркеров"
docker-compose stop worker2
docker-compose stop worker3
docker-compose stop worker4
sleep 50

echo "Логи"
docker-compose logs

docker-compose down 