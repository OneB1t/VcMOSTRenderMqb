#!/bin/sh
# Usage: start.sh [host [port]]
# Defaults come from config.txt in /fs/sda0.
# Optional env vars:
#   CAPTURE_LOG=1   Always capture stdout/stderr to a logfile.
#   LOG_FILE=...    Override logfile path (if writable).
#   USE_SYSLOG=1    Enable app syslog sink (RENDER_SYSLOG=1).
#   SYSLOG_REMOTE=host[:port]  Send logs directly to remote syslog receiver.
#   SYSLOG_HOST=host           Remote syslog host override.
#   SYSLOG_PORT=port           Remote syslog port override (default 514).
# App flags passed through unchanged:
#   --syslog-remote=host[:port]
#   --syslog-host=host
#   --syslog-port=port

export IPL_CONFIG_DIR=/etc/eso/production

cd /fs/sda0 || exit 1

capture_log=0
use_syslog=0

if [ "${USE_SYSLOG:-0}" = "1" ]; then
	use_syslog=1
fi

if [ -n "${SYSLOG_REMOTE:-}" ]; then
	export RENDER_SYSLOG_REMOTE="$SYSLOG_REMOTE"
fi
if [ -n "${SYSLOG_HOST:-}" ]; then
	export RENDER_SYSLOG_HOST="$SYSLOG_HOST"
fi
if [ -n "${SYSLOG_PORT:-}" ]; then
	export RENDER_SYSLOG_PORT="$SYSLOG_PORT"
fi

if [ "${CAPTURE_LOG:-0}" = "1" ]; then
	capture_log=1
fi

for arg in "$@"; do
	if [ "$arg" = "--verbose" ]; then
		capture_log=1
	elif [ "$arg" = "--syslog" ]; then
		use_syslog=1
	elif [ "$arg" = "--no-syslog" ]; then
		use_syslog=0
	fi
done

if [ "$capture_log" = "1" ]; then
	log_file=""
	if [ -n "${LOG_FILE:-}" ]; then
		log_file="$LOG_FILE"
	elif [ -w /tmp ]; then
		log_file="/tmp/opengl-render-qnx.log"
	elif [ -w /var/tmp ]; then
		log_file="/var/tmp/opengl-render-qnx.log"
	fi

	if [ -n "$log_file" ]; then
		{
			echo ""
			echo "===== $(date) ====="
			echo "cmd: ./opengl-render-qnx-h264 $*"
		} >> "$log_file" 2>/dev/null

		if [ "$use_syslog" = "1" ]; then
			RENDER_SYSLOG=1 nohup ./opengl-render-qnx-h264 "$@" >> "$log_file" 2>&1 &
			echo "started pid $! (logging to $log_file, syslog enabled)"
		else
			nohup ./opengl-render-qnx-h264 "$@" >> "$log_file" 2>&1 &
			echo "started pid $! (logging to $log_file)"
		fi
	else
		echo "no writable log target found; streaming in foreground"
		if [ "$use_syslog" = "1" ]; then
			exec env RENDER_SYSLOG=1 ./opengl-render-qnx-h264 "$@"
		else
			exec ./opengl-render-qnx-h264 "$@"
		fi
	fi
else
	if [ "$use_syslog" = "1" ]; then
		RENDER_SYSLOG=1 nohup ./opengl-render-qnx-h264 "$@" > /dev/null 2>&1 &
		echo "started pid $! (syslog enabled)"
	else
		nohup ./opengl-render-qnx-h264 "$@" > /dev/null 2>&1 &
		echo "started pid $!"
	fi
fi
