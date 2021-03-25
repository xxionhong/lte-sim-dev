# /bin/bash
_user="$(id -u -n)"
docker run -it -v /home/$_user/lte-sim-dev:/root/lte-sim-dev -w /root/lte-sim-dev --rm ltesim:lastest
