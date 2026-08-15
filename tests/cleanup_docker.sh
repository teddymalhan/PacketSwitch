#!/bin/bash


echo "Cleaning up Docker test containers..."


echo "Stopping containers..."
docker stop $(docker ps -q --filter "name=vswitch\|vport") 2>/dev/null || true


echo "Removing containers..."
docker rm $(docker ps -aq --filter "name=vswitch\|vport") 2>/dev/null || true


for name in vswitch-test vport-test vswitch vport1 vport2 vport-tp1 vport-tp2 vswitch-tp vport-scale vswitch-scale; do
  docker rm -f $name 2>/dev/null || true
done


docker ps -aq | while read id; do
  name=$(docker inspect --format '{{.Name}}' $id 2>/dev/null | sed 's/\///')
  if [[ "$name" =~ test$ ]] || [[ "$name" =~ -test$ ]]; then
    docker rm -f $id 2>/dev/null || true
  fi
done

echo "Cleanup complete!"
echo ""
echo "Remaining containers:"
docker ps -a

