/***************************************************************************
 *   fheroes2: https://github.com/ihhub/fheroes2                           *
 *   Copyright (C) 2020 - 2024                                             *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "agg_file.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iterator>
#include <string>

namespace fheroes2
{
    // HoMM1 AGG format (kaitai.io spec):
    //   num_files  : u2
    //   entries[]  : num_files × { hash(u2), offset(u4), size(u4), size2(u4) }  = 14 bytes each
    //   file data
    //   filenames  : num_files × 15-byte null-padded strings at end of file
    //   Filenames are stored lowercase; we normalise to uppercase on insert.
    //
    // HoMM2 AGG format:
    //   num_files  : u2
    //   entries[]  : num_files × { hash(u4), offset(u4), size(u4) }             = 12 bytes each
    //   file data
    //   filenames  : num_files × 15-byte null-padded strings at end of file
    //   Filenames are stored uppercase; hash verified on load.

    static constexpr size_t _homm1EntrySize = sizeof( uint16_t ) + sizeof( uint32_t ) * 3; // 14 bytes
    static constexpr size_t _homm2EntrySize = sizeof( uint32_t ) * 3;                       // 12 bytes

    static void normalizeToUpper( std::string & s )
    {
        for ( char & c : s ) {
            c = static_cast<char>( std::toupper( static_cast<unsigned char>( c ) ) );
        }
    }

    bool AGGFile::_openHoMM2( const size_t count, const size_t size )
    {
        const size_t nameEntriesSize = _maxFilenameSize * count;

        if ( count * ( _homm2EntrySize + _maxFilenameSize ) >= size ) {
            return false;
        }

        ROStreamBuf fileEntries = _stream.getStreamBuf( count * _homm2EntrySize );
        _stream.seek( size - nameEntriesSize );
        ROStreamBuf nameEntries = _stream.getStreamBuf( nameEntriesSize );

        for ( size_t i = 0; i < count; ++i ) {
            std::string name = nameEntries.getString( _maxFilenameSize );
            normalizeToUpper( name );

            // Verify 32-bit filename hash.
            if ( fileEntries.getLE32() != calculateAggFilenameHash( name ) ) {
                _files.clear();
                return false;
            }

            const uint32_t fileOffset = fileEntries.getLE32();
            const uint32_t fileSize   = fileEntries.getLE32();
            _files.try_emplace( std::move( name ), std::make_pair( fileSize, fileOffset ) );
        }

        if ( _files.size() != count ) {
            _files.clear();
            return false;
        }

        return !_stream.fail();
    }

    bool AGGFile::_openHoMM1( const size_t count, const size_t size )
    {
        const size_t nameEntriesSize = _maxFilenameSize * count;

        if ( count * ( _homm1EntrySize + _maxFilenameSize ) >= size ) {
            return false;
        }

        // FAT: hash(u2) + offset(u4) + size(u4) + size2(u4) per entry.
        ROStreamBuf fileEntries = _stream.getStreamBuf( count * _homm1EntrySize );

        // Name table: positionally matched with FAT entries.
        _stream.seek( size - nameEntriesSize );
        ROStreamBuf nameEntries = _stream.getStreamBuf( nameEntriesSize );

        for ( size_t i = 0; i < count; ++i ) {
            std::string name = nameEntries.getString( _maxFilenameSize );
            normalizeToUpper( name );  // HoMM1 stores names lowercase; normalise to uppercase

            fileEntries.getLE16(); // hash(u2) — skip; positional match is used
            const uint32_t fileOffset = fileEntries.getLE32();
            const uint32_t fileSize   = fileEntries.getLE32();
            fileEntries.getLE32();  // size2 — always equals fileSize, discard

            if ( !name.empty() ) {
                _files.try_emplace( std::move( name ), std::make_pair( fileSize, fileOffset ) );
            }
        }

        if ( _files.empty() ) {
            return false;
        }

        return !_stream.fail();
    }

    bool AGGFile::open( const std::string & fileName )
    {
        if ( !_stream.open( fileName, "rb" ) ) {
            return false;
        }

        const size_t size     = _stream.size();
        const size_t count    = _stream.getLE16();
        const size_t fatStart = _stream.tell();

        // Try HoMM2 format first (12-byte entries, 32-bit hash verification).
        if ( _openHoMM2( count, size ) ) {
            return true;
        }

        // Fall back to HoMM1 format (14-byte entries, 2-byte hash, positional name match).
        _stream.seek( fatStart );
        _files.clear();
        return _openHoMM1( count, size );
    }

    std::vector<std::string> AGGFile::getFileNames() const
    {
        std::vector<std::string> result;
        result.reserve( _files.size() );

        for ( const auto & [name, _] : _files ) {
            result.emplace_back( name );
        }

        return result;
    }

    std::vector<std::string> AGGFile::getFileNamesWithExtension( std::string_view ext ) const
    {
        std::vector<std::string> result;
        for ( const auto & [name, _] : _files ) {
            if ( name.size() >= ext.size() ) {
                const std::string suffix = name.substr( name.size() - ext.size() );
                bool match = ( suffix.size() == ext.size() );
                for ( size_t i = 0; match && i < suffix.size(); ++i ) {
                    match = ( std::toupper( static_cast<unsigned char>( suffix[i] ) )
                              == std::toupper( static_cast<unsigned char>( ext[i] ) ) );
                }
                if ( match ) {
                    result.emplace_back( name );
                }
            }
        }
        return result;
    }

    std::vector<uint8_t> AGGFile::read( const std::string & fileName )
    {
        auto it = _files.find( fileName );
        if ( it == _files.end() ) {
            return {};
        }

        const auto [fileSize, fileOffset] = it->second;
        if ( fileSize > 0 ) {
            _stream.seek( fileOffset );
            return _stream.getRaw( fileSize );
        }

        return {};
    }

    uint32_t calculateAggFilenameHash( const std::string_view str )
    {
        uint32_t hash = 0;
        uint32_t sum  = 0;

        for ( auto iter = str.rbegin(); iter != str.rend(); ++iter ) {
            const unsigned char c = static_cast<unsigned char>( std::toupper( static_cast<unsigned char>( *iter ) ) );

            hash = ( hash << 5 ) + ( hash >> 25 );

            sum += c;
            hash += sum + c;
        }

        return hash;
    }
}

IStreamBase & operator>>( IStreamBase & stream, fheroes2::ICNHeader & icn )
{
    icn.offsetX         = static_cast<int16_t>( stream.getLE16() );
    icn.offsetY         = static_cast<int16_t>( stream.getLE16() );
    icn.width           = stream.getLE16();
    icn.height          = stream.getLE16();
    icn.animationFrames = stream.get();
    icn.offsetData      = stream.getLE32();

    return stream;
}
