#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <syslog.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pulse/simple.h>
#include <pulse/error.h>
#include <cjson/cJSON.h>
#include <rnnoise.h>
#include <speex/speex_echo.h>

#define MIC_BUFFER_FRAMES_PER_CHANNEL 480
#define SOCK_PATH "/var/run/micarrayd.socket"
#define SOCK2_PATH "/var/run/micarrayd.socket2"

struct mic {
	pa_simple *handle;
	pa_sample_spec sample_spec;
	char *interface;
	DenoiseState *rnnoise;
	SpeexEchoState *st;
	int16_t buffer[];
};

/*
inline float remap(float value, float lowin, float highin, float lowout, float highout)
{
	return lowout + (value - lowin) * (highout - lowout) / (highin - lowin);
}
*/

int micarrayd(volatile int *stop, int argc, char *argv[])
{
	int err;

	int fd;
	struct stat st;
	char *confbuf;

	unsigned micrate;
	int micfmt;
	unsigned spkrrate;
	int spkrfmt;

	int nmics;
	struct mic **mics;

	int sock, conn;
	struct sockaddr_un sockaddr;
	
	float mictmpbuf[MIC_BUFFER_FRAMES_PER_CHANNEL];
	int16_t spkrtmpbuf[MIC_BUFFER_FRAMES_PER_CHANNEL];

	cJSON *config;
	cJSON *micconf, *micconf_mics, *micconf_rate, *micconf_format;
	cJSON *spkrconf, *spkrconf_interface, *spkrconf_rate, *spkrconf_format, *spkrconf_channels;
	cJSON *conf_noise_cancelling, *conf_echo_cancelling;

	int noise_cancelling, echo_cancelling;

	int spkrchannels;
	char *spkrinterface;

	pa_simple *spkr_handle;
	pa_sample_spec spkr_sample_spec;

	DenoiseState *rnnoise;

	syslog(LOG_INFO, "starting micarrayd");

	if ((fd = open("/etc/micarrayd.json", O_RDONLY)) < 0) {
		syslog(LOG_ERR, "can not open /etc/micarrayd.json");
		return -1;
	}

	if (fstat(fd, &st) < 0) {
		syslog(LOG_ERR, "can not stat /etc/micarrayd.json");
		return -1;
	}

	if (!(confbuf = malloc(st.st_size))) {
		syslog(LOG_ERR, "can not allocate memory to read /etc/micarrayd.json");
		return -1;
	}

	if (read(fd, confbuf, st.st_size) != st.st_size) {
		syslog(LOG_ERR, "can not read /etc/micarrayd.json");
		return -1;
	}

	close(fd);

	if (!(config = cJSON_ParseWithLength(confbuf, st.st_size))) {
		syslog(LOG_ERR, "can not parse /etc/micarrayd.json");
		return -1;
	}

	free(confbuf);

	micconf = cJSON_GetObjectItemCaseSensitive(config, "micconf");
	spkrconf = cJSON_GetObjectItemCaseSensitive(config, "spkrconf");
	conf_noise_cancelling = cJSON_GetObjectItemCaseSensitive(config, "noise_cancelling");
	conf_echo_cancelling = cJSON_GetObjectItemCaseSensitive(config, "echo_cancelling");

	if (!micconf || !spkrconf || !conf_noise_cancelling || !conf_echo_cancelling ||
			!cJSON_IsBool(conf_noise_cancelling) || !cJSON_IsBool(conf_echo_cancelling)) {
		syslog(LOG_ERR, "can not parse /etc/micarrayd.json");
		return -1;
	}

	noise_cancelling = cJSON_IsTrue(conf_noise_cancelling);
	echo_cancelling = cJSON_IsTrue(conf_echo_cancelling);

	micconf_mics = cJSON_GetObjectItemCaseSensitive(micconf, "mics");
	micconf_rate = cJSON_GetObjectItemCaseSensitive(micconf, "rate");
	micconf_format = cJSON_GetObjectItemCaseSensitive(micconf, "format");

	spkrconf_interface = cJSON_GetObjectItemCaseSensitive(spkrconf, "interface");
	spkrconf_rate = cJSON_GetObjectItemCaseSensitive(spkrconf, "rate");
	spkrconf_format = cJSON_GetObjectItemCaseSensitive(spkrconf, "format");
	spkrconf_channels = cJSON_GetObjectItemCaseSensitive(spkrconf, "channels");

	if (!micconf_mics || !micconf_rate || !micconf_format ||
			!spkrconf_interface || !spkrconf_rate || !spkrconf_format || !spkrconf_channels) {
		syslog(LOG_ERR, "can not parse /etc/micarrayd.json");
		return -1;
	}

	if (!cJSON_IsNumber(micconf_rate) || !cJSON_IsNumber(spkrconf_rate) || !cJSON_IsArray(micconf_mics) ||
			!cJSON_IsString(micconf_format) || !cJSON_IsString(spkrconf_format) ||
			!cJSON_IsNumber(spkrconf_rate) || !cJSON_IsString(spkrconf_interface)) {
		syslog(LOG_ERR, "can not parse /etc/micarrayd.json");
		return -1;
	}

	micrate = micconf_rate->valueint;

	spkrinterface = spkrconf_interface->valuestring;
	spkrrate = spkrconf_rate->valueint;
	spkrchannels = spkrconf_channels->valueint;

	spkr_sample_spec.format = PA_SAMPLE_S16LE;
	spkr_sample_spec.rate = spkrrate;
	spkr_sample_spec.channels = spkrchannels;

	nmics = cJSON_GetArraySize(micconf_mics);

	if (!(mics = malloc(sizeof(*mics) * nmics))) {
		syslog(LOG_ERR, "can not allocate enough memory");
		return -1;
	}

	for (size_t i = 0; i < nmics; i++) {
		cJSON *curmic, *curmic_interface, *curmic_channels;
		char *interface;
		unsigned channels;

		curmic = cJSON_GetArrayItem(micconf_mics, i);
		curmic_interface = cJSON_GetObjectItemCaseSensitive(curmic, "interface");
		curmic_channels = cJSON_GetObjectItemCaseSensitive(curmic, "channels");
		if (!curmic_interface || !curmic_channels ||
				!cJSON_IsString(curmic_interface) || !cJSON_IsNumber(curmic_channels)) {
			syslog(LOG_ERR, "can not parse /etc/micarrayd.json");
			return -1;
		}

		interface = curmic_interface->valuestring;
		channels = curmic_channels->valueint;

		if (!(mics[i] = malloc(sizeof(**mics) + sizeof(int16_t) *
						channels * MIC_BUFFER_FRAMES_PER_CHANNEL))) {
			syslog(LOG_ERR, "can not allocate enough memory");
			return -1;
		}

		mics[i]->sample_spec.format = PA_SAMPLE_S16LE;
		mics[i]->sample_spec.rate = 48000;
		mics[i]->sample_spec.channels = channels;
		mics[i]->handle = NULL;
		mics[i]->interface = interface;
		if (!(mics[i]->rnnoise = rnnoise_create(NULL))) {
			syslog(LOG_ERR, "can not init rnnoise");
			return -1;
		}
		mics[i]->st = speex_echo_state_init(MIC_BUFFER_FRAMES_PER_CHANNEL, MIC_BUFFER_FRAMES_PER_CHANNEL / 2);
		if (!mics[i]->st) {
			syslog(LOG_ERR, "can not init speex");
			return -1;
		}
	}

	if ((sock = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0) {
		syslog(LOG_ERR, "can not open socket");
		return -1;
	}

	sockaddr.sun_family = AF_UNIX;
	strcpy(sockaddr.sun_path, SOCK_PATH);

	unlink(SOCK_PATH);

	if (bind(sock, (struct sockaddr *) &sockaddr, sizeof(sockaddr)) < 0) {
		syslog(LOG_ERR, "can not bind socket");
		return -1;
	}

	if (listen(sock, 1) < 0) {
		syslog(LOG_ERR, "can not listen socket");
		return -1;
	}

	conn = -1;

	int sock2, conn2;
	if ((sock2 = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0) {
		syslog(LOG_ERR, "can not open socket");
		return -1;
	}

	sockaddr.sun_family = AF_UNIX;
	strcpy(sockaddr.sun_path, SOCK2_PATH);

	unlink(SOCK2_PATH);

	if (bind(sock2, (struct sockaddr *) &sockaddr, sizeof(sockaddr)) < 0) {
		syslog(LOG_ERR, "can not bind socket");
		return -1;
	}

	if (listen(sock2, 1) < 0) {
		syslog(LOG_ERR, "can not listen socket");
		return -1;
	}

	conn2 = -1;

	while (!*stop) {
		if (conn < 0) {
			conn = accept(sock, NULL, NULL);
			if (conn >= 0) {
				for (size_t i = 0; i < nmics; i++) {
					mics[i]->handle = pa_simple_new(NULL, "micarrayd",
							PA_STREAM_RECORD, mics[i]->interface, "record",
							&mics[i]->sample_spec, NULL, NULL, &err);
					if (!mics[i]->handle) {
						syslog(LOG_ERR, "pa_simple_new() error %s", pa_strerror(err));
						return -1;
					}
				}
			}
		}

		if (conn2 < 0) {
			conn2 = accept(sock2, NULL, NULL);
			if (conn2 >= 0) {
				spkr_handle = pa_simple_new(NULL, "micarrayd",
						PA_STREAM_PLAYBACK, spkrinterface, "playback",
						&spkr_sample_spec, NULL, NULL, &err);
				if (!spkr_handle) {
					syslog(LOG_ERR, "pa_simple_new() error %s", pa_strerror(err));
					return -1;
				}
			}
		}

		ssize_t nbytes;
		bool spkrw = false;
		if (conn2 >= 0) {
			err = nbytes = recv(conn2, spkrtmpbuf, MIC_BUFFER_FRAMES_PER_CHANNEL * sizeof(int16_t), MSG_DONTWAIT);
			if (err < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
				spkrw = false;
				;
			} else if (err <= 0) {
				spkrw = false;
				close(conn2);
				conn2 = -1;
				pa_simple_free(spkr_handle);
				continue;
			} else {
				spkrw = true;
				err = pa_simple_write(spkr_handle, spkrtmpbuf, nbytes, &err);
				if (err < 0) {
					syslog(LOG_ERR, "pa_simple_write() error %s", pa_strerror(err));
					return -1;
				}
			}
		}

		if (conn >= 0) {
			for (size_t i = 0; i < nmics; i++) {
				err = pa_simple_read(mics[i]->handle, mics[i]->buffer,
						MIC_BUFFER_FRAMES_PER_CHANNEL * mics[i]->sample_spec.channels * sizeof(int16_t), &err);
				if (err < 0) {
					syslog(LOG_ERR, "pa_simple_read() error");
					return -1;
				}

				if (noise_cancelling) {
					for (size_t j = 0; j < MIC_BUFFER_FRAMES_PER_CHANNEL; j++) {
						mictmpbuf[j] = mics[i]->buffer[j];
					}
					rnnoise_process_frame(mics[i]->rnnoise, mictmpbuf, mictmpbuf);
					for (size_t j = 0; j < MIC_BUFFER_FRAMES_PER_CHANNEL; j++) {
						mics[i]->buffer[j] = mictmpbuf[j];
					}
				}

				if (echo_cancelling && spkrw) {
					speex_echo_cancellation(mics[i]->st, mics[i]->buffer, spkrtmpbuf, mics[i]->buffer);
				}
			}

			for (size_t i = 0; i < MIC_BUFFER_FRAMES_PER_CHANNEL; i++) {
				for (size_t j = 0; j < nmics; j++) {
					for (size_t k = 0; k < mics[j]->sample_spec.channels; k++) {
						int16_t frame = mics[j]->buffer[i * mics[j]->sample_spec.channels + k];
						if (conn >= 0) {
							if ((err = send(conn, &frame, sizeof(int16_t), 0)) !=
									sizeof(int16_t)) {
								close(conn);
								conn = -1;
								for (size_t i = 0; i < nmics; i++) {
									pa_simple_free(mics[i]->handle);
								}
								goto new_conn;
							}
						}
					}
				}
			}
		}

new_conn:
	}

	syslog(LOG_INFO, "stopping micarrayd");

	return 0;
}

