#!/usr/bin/env bash
PS4_IP="${1:?Usage: ./stop.sh <PS4_IP>}"
nc -zv "$PS4_IP" 5901 || true
