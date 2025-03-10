// Copyright 2009-2022 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_PACKETDATASTREAM_H_
#define MUMBLE_PACKETDATASTREAM_H_

#include <QByteArray>
#include <QPair>
#include <QString>

#include <cstring>

/*
 * GCC doesn't yet do inter-object-file inlining, so unfortunately, this all has to be defined here.
 */

class PacketDataStream {
private:
	Q_DISABLE_COPY(PacketDataStream)
private:
	unsigned char *data;
	quint32 maxsize;
	quint32 offset;
	quint32 overshoot;
	bool ok;

public:
	quint32 size() const { return offset; }

	quint32 capacity() const { return maxsize; }

	bool isValid() const { return ok; }

	quint32 left() const { return maxsize - offset; }

	quint32 undersize() const { return overshoot; }

	void append(const quint64 v) {
#ifndef QT_NO_DEBUG
		Q_ASSERT(v <= 0xff);
#endif
		if (offset < maxsize)
			data[offset++] = static_cast< unsigned char >(v);
		else {
			ok = false;
			overshoot++;
		}
	};

	void append(const char *d, quint32 len) {
		if (left() >= len) {
			memcpy(&data[offset], d, len);
			offset += len;
		} else {
			int l = left();
			memset(&data[offset], 0, l);
			offset += l;
			overshoot += len - l;
			ok = false;
		}
	}

	void skip(quint32 len) {
		if (left() >= len)
			offset += len;
		else
			ok = false;
	}

	quint64 next() {
		if (offset < maxsize)
			return data[offset++];
		else {
			ok = false;
			return 0;
		}
	};

	quint8 next8() {
		if (offset < maxsize)
			return data[offset++];
		else {
			ok = false;
			return 0;
		}
	}

	void rewind() { offset = 0; }

	void truncate() { maxsize = offset; }

	const unsigned char *dataPtr() const { return reinterpret_cast< const unsigned char * >(&data[offset]); }

	const char *charPtr() const { return reinterpret_cast< const char * >(&data[offset]); }

	QByteArray dataBlock(quint32 len) {
		if (len <= left()) {
			QByteArray a(charPtr(), len);
			offset += len;
			return a;
		} else {
			ok = false;
			return QByteArray();
		}
	}

protected:
	void setup(unsigned char *d, int msize) {
		data      = d;
		offset    = 0;
		overshoot = 0;
		maxsize   = msize;
		ok        = true;
	}

public:
	PacketDataStream(const char *d, int msize) {
		setup(const_cast< unsigned char * >(reinterpret_cast< const unsigned char * >(d)), msize);
	};

	PacketDataStream(char *d, int msize) { setup(reinterpret_cast< unsigned char * >(d), msize); };

	PacketDataStream(unsigned char *d, int msize) { setup(d, msize); };

	PacketDataStream(const QByteArray &qba) {
		setup(const_cast< unsigned char * >(reinterpret_cast< const unsigned char * >(qba.constData())), qba.size());
	}

	PacketDataStream(QByteArray &qba) {
		unsigned char *ptr = reinterpret_cast< unsigned char * >(qba.data());
		setup(ptr, qba.capacity());
	}

	PacketDataStream &operator<<(const quint64 value) {
		quint64 i = value;

		if ((i & 0x8000000000000000LL) && (~i < 0x100000000LL)) {
			// Signed number.
			i = ~i;
			if (i <= 0x3) {
				// Shortcase for -1 to -4
				append(0xFC | i);
				return *this;
			} else {
				append(0xF8);
			}
		}
		if (i < 0x80) {
			// Need top bit clear
			append(i);
		} else if (i < 0x4000) {
			// Need top two bits clear
			append((i >> 8) | 0x80);
			append(i & 0xFF);
		} else if (i < 0x200000) {
			// Need top three bits clear
			append((i >> 16) | 0xC0);
			append((i >> 8) & 0xFF);
			append(i & 0xFF);
		} else if (i < 0x10000000) {
			// Need top four bits clear
			append((i >> 24) | 0xE0);
			append((i >> 16) & 0xFF);
			append((i >> 8) & 0xFF);
			append(i & 0xFF);
		} else if (i < 0x100000000LL) {
			// It's a full 32-bit integer.
			append(0xF0);
			append((i >> 24) & 0xFF);
			append((i >> 16) & 0xFF);
			append((i >> 8) & 0xFF);
			append(i & 0xFF);
		} else {
			// It's a 64-bit value.
			append(0xF4);
			append((i >> 56) & 0xFF);
			append((i >> 48) & 0xFF);
			append((i >> 40) & 0xFF);
			append((i >> 32) & 0xFF);
			append((i >> 24) & 0xFF);
			append((i >> 16) & 0xFF);
			append((i >> 8) & 0xFF);
			append(i & 0xFF);
		}
		return *this;
	}

	PacketDataStream &operator>>(quint64 &i) {
		quint64 v = next();

		if ((v & 0x80) == 0x00) {
			i = (v & 0x7F);
		} else if ((v & 0xC0) == 0x80) {
			i = (v & 0x3F) << 8 | next();
		} else if ((v & 0xF0) == 0xF0) {
			switch (v & 0xFC) {
				case 0xF0:
					i = next() << 24 | next() << 16 | next() << 8 | next();
					break;
				case 0xF4:
					i = next() << 56 | next() << 48 | next() << 40 | next() << 32 | next() << 24 | next() << 16
						| next() << 8 | next();
					break;
				case 0xF8:
					*this >> i;
					i = ~i;
					break;
				case 0xFC:
					i = v & 0x03;
					i = ~i;
					break;
				default:
					ok = false;
					i  = 0;
					break;
			}
		} else if ((v & 0xF0) == 0xE0) {
			i = (v & 0x0F) << 24 | next() << 16 | next() << 8 | next();
		} else if ((v & 0xE0) == 0xC0) {
			i = (v & 0x1F) << 16 | next() << 8 | next();
		}
		return *this;
	}

	PacketDataStream &operator<<(const QByteArray &a) {
		*this << a.size();
		append(a.constData(), a.size());
		return *this;
	}

	PacketDataStream &operator>>(QByteArray &a) {
		quint32 len;
		*this >> len;
		if (len > left()) {
			len = left();
			ok  = false;
		}
		a = QByteArray(reinterpret_cast< const char * >(&data[offset]), len);
		offset += len;
		return *this;
	}

	PacketDataStream &operator<<(const QString &s) { return *this << s.toUtf8(); }

	// Using the data directly instead of through qbuff avoids a copy.
	PacketDataStream &operator>>(QString &s) {
		quint32 len;
		*this >> len;
		if (len > left()) {
			len = left();
			ok  = false;
		}
		s = QString::fromUtf8(reinterpret_cast< const char * >(&data[offset]), len);
		offset += len;
		return *this;
	}

	PacketDataStream &operator<<(const bool b) {
		quint32 v = b ? 1 : 0;
		return *this << v;
	}

	PacketDataStream &operator>>(bool &b) {
		quint32 v;
		*this >> v;
		b = v ? true : false;
		return *this;
	}

#define INTMAPOPERATOR(type)                                                                  \
	PacketDataStream &operator<<(const type v) { return *this << static_cast< quint64 >(v); } \
	PacketDataStream &operator>>(type &v) {                                                   \
		quint64 vv;                                                                           \
		*this >> vv;                                                                          \
		v = static_cast< type >(vv);                                                          \
		return *this;                                                                         \
	}


	INTMAPOPERATOR(int);
	INTMAPOPERATOR(unsigned int);
	INTMAPOPERATOR(short);
	INTMAPOPERATOR(unsigned short);
	INTMAPOPERATOR(char);
	INTMAPOPERATOR(unsigned char);

	union double64u {
		quint64 ui;
		double d;
	};

	PacketDataStream &operator<<(const double v) {
		double64u u;
		u.d = v;
		return *this << u.ui;
	}

	PacketDataStream &operator>>(double &v) {
		double64u u;
		*this >> u.ui;
		v = u.d;
		return *this;
	}

	union float32u {
		quint8 ui[4];
		float f;
	};

	PacketDataStream &operator<<(const float v) {
		float32u u;
		u.f = v;
		append(u.ui[0]);
		append(u.ui[1]);
		append(u.ui[2]);
		append(u.ui[3]);
		return *this;
	}

	PacketDataStream &operator>>(float &v) {
		float32u u;
		if (left() < 4) {
			ok = false;
			v  = 0;
		}
		u.ui[0] = next8();
		u.ui[1] = next8();
		u.ui[2] = next8();
		u.ui[3] = next8();
		v       = u.f;
		return *this;
	}

	template< typename T > PacketDataStream &operator<<(const QList< T > &l) {
		*this << l.size();
		for (int i = 0; i < l.size(); i++)
			*this << l.at(i);
		return *this;
	}

	template< typename T > PacketDataStream &operator>>(QList< T > &l) {
		l.clear();
		quint32 len;
		*this >> len;
		if (len > left()) {
			len = left();
			ok  = false;
		}
		for (quint32 i = 0; i < len; i++) {
			if (left() == 0) {
				ok = false;
				break;
			}

			T t;
			*this >> t;
			l.append(t);
		}
		return *this;
	}


	template< typename T > PacketDataStream &operator<<(const QSet< T > &s) {
		*this << s.size();
		for (typename QSet< T >::const_iterator i = s.constBegin(); i != s.constEnd(); ++i)
			*this << *i;
		return *this;
	}

	template< typename T > PacketDataStream &operator>>(QSet< T > &s) {
		s.clear();
		quint32 len;
		*this >> len;
		if (len > left()) {
			len = left();
			ok  = false;
		}
		for (quint32 i = 0; i < len; i++) {
			if (left() == 0) {
				ok = false;
				break;
			}

			T t;
			*this >> t;
			s.insert(t);
		}
		return *this;
	}

	template< typename T, typename U > PacketDataStream &operator<<(const QPair< T, U > &p) {
		return *this << p.first << p.second;
	}

	template< typename T, typename U > PacketDataStream &operator>>(QPair< T, U > &p) {
		return *this >> p.first >> p.second;
	}
};

#endif
