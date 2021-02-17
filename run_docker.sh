# /bin/bash
_user="$(id -u -n)"
docker run -it -v /home/$_user/lte-sim-dev:/root/lte-sim -w /root/lte-sim --rm ltesim:lastest