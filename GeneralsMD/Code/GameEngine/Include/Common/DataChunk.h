/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// DataChunk.h
// Data chunk classes for saving and loading
// Author: Michael S. Booth, October 2000

#pragma once

#include "Common/GameMemory.h"
#include "Common/Dict.h"
#include "Common/MapReaderWriterInfo.h"

typedef unsigned short DataChunkVersionType;

// BFME: no version ceiling. A chunk version is an unsigned short, so none is greater than this.
static const DataChunkVersionType DATA_CHUNK_VERSION_ANY = 0xFFFF;

// forward declarations
class DataChunkInput;
class DataChunkOutput;
class DataChunkTableOfContents;

//----------------------------------------------------------------------
// Mapping
//----------------------------------------------------------------------
class Mapping : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(Mapping, "Mapping")
public:
	Mapping*			next;
	AsciiString		name;
	UnsignedInt		id;
};
EMPTY_DTOR(Mapping)

//----------------------------------------------------------------------
// OutputChunk
//----------------------------------------------------------------------
class OutputChunk : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(OutputChunk, "OutputChunk")
public:
	OutputChunk*	next;
	UnsignedInt		id;															// chunk symbol type from table of contents
	Int						filepos;											// position of file at start of data offset
};
EMPTY_DTOR(OutputChunk)

//----------------------------------------------------------------------
// InputChunk
//----------------------------------------------------------------------
class InputChunk : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(InputChunk, "InputChunk")
public:
	InputChunk*						next;
	UnsignedInt						id;															// chunk symbol type from table of contents
	DataChunkVersionType	version;									// version of data
	Int										chunkStart;																// tell position of the start of chunk data (past header).
	Int										dataSize;														// total data size of chunk
	Int										dataLeft;														// data left to read in this chunk
};
EMPTY_DTOR(InputChunk)

//----------------------------------------------------------------------
// DataChunkTableOfContents
//----------------------------------------------------------------------
class DataChunkTableOfContents
{
	Mapping*		m_list;																/// @TODO: This should be a hash table
	Int					m_listLength;
	UnsignedInt	m_nextID;											// simple ID allocator
	Bool				m_headerOpened;

	Mapping *findMapping( const AsciiString& name );			// return mapping data

public:
	DataChunkTableOfContents();
	~DataChunkTableOfContents();

	UnsignedInt getID( const AsciiString& name );				// convert name to integer identifier
	AsciiString getName( UnsignedInt id );	// convert integer identifier to name
	UnsignedInt allocateID( const AsciiString& name );		// create new ID for given name or return existing mapping

	Bool isOpenedForRead() {return m_headerOpened;};

	void write(OutputStream &out);
	void read(ChunkInputStream &in);
};


//----------------------------------------------------------------------
// DataChunkOutput
//----------------------------------------------------------------------
class DataChunkOutput
{
protected:
	OutputStream*							m_pOut;										// The actual output stream.
	FILE *										m_tmp_file;												// tmp output file stream
	DataChunkTableOfContents	m_contents;			// table of contents of data chunk types
	OutputChunk*							m_chunkStack;													// current stack of open data chunks

public:
	DataChunkOutput(  OutputStream *pOut  );
	~DataChunkOutput();

	void openDataChunk( const char *name, DataChunkVersionType ver );
	void closeDataChunk();

	void writeReal(Real r);
	void writeInt(Int i);
	void writeByte(Byte b);
	void writeAsciiString(const AsciiString& string);
	void writeUnicodeString(UnicodeString string);
	void writeArrayOfBytes(char *ptr, Int len);
	void writeDict(const Dict& d);
	void writeNameKey(const NameKeyType key);
};

//----------------------------------------------------------------------
// DataChunkInput
//----------------------------------------------------------------------
struct DataChunkInfo
{
	AsciiString label;
	AsciiString parentLabel;
	DataChunkVersionType version;
	Int dataSize;
};

typedef Bool (*DataChunkParserPtr)( DataChunkInput &file, DataChunkInfo *info, void *userData );

//----------------------------------------------------------------------
// UserParser
//----------------------------------------------------------------------
class UserParser : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(UserParser, "UserParser")
public:
	UserParser *next;

	DataChunkParserPtr parser;										// the user parsing function
	AsciiString label;																	// the data chunk label to match
	AsciiString parentLabel;														// the parent chunk's label (the scope)
	void *userData;																	// user data pointer
	// BFME: versions above this are an error. DATA_CHUNK_VERSION_ANY means the parser accepts any version.
	DataChunkVersionType maxVersion;
};
EMPTY_DTOR(UserParser)

//----------------------------------------------------------------------
// DataChunkInput
//----------------------------------------------------------------------
class DataChunkInput
{
	enum {CHUNK_HEADER_BYTES = 4}; // 2 shorts in chunk file header.
protected:
	ChunkInputStream*					m_file;															// input file stream
	DataChunkTableOfContents	m_contents;							// table of contents of data chunk types
	Int												m_fileposOfFirstChunk;										// seek position of first data chunk
	UserParser*								m_parserList;																		// list of all registered parsers for this input stream
	InputChunk*								m_chunkStack;																		// current stack of open data chunks
	Int												m_skippedChunkCount;														// BFME: unknown skips on this file (names not in mapChunkVersionCeiling)

	void clearChunkStack();										// clear the stack

	void decrementDataLeft( int size );							// update data left in chunk(s)

public:
	void *m_currentObject;														// user parse routines can use this to allow one chunk
																									// to create an object, and a subsequent chunk to
																									// parse values into that object.  However, the second
																									// chunk parser could also create and parse an object
																									// of its own if this pointer is null.
																									// The parser of the base class should set this pointer to null.
	void *m_userData;																	// user data hook

public:
	DataChunkInput( ChunkInputStream *pStream );
	~DataChunkInput();

	// register a parser function for data chunks with labels matching "label", whose parent
	// chunks labels match "parentLabel" (or null for global scope).
	// BFME: a version above maxVersion is logged and throws ERROR_CORRUPT_FILE_FORMAT before the
	// parser runs. For map chunks that maximum is the BFME1 data scan (mapChunkVersionCeiling),
	// which can be newer than the layout this parser implements. DATA_CHUNK_VERSION_ANY (the
	// default) leaves the version unchecked.
	void registerParser( const AsciiString& label, const AsciiString& parentLabel, DataChunkParserPtr parser, void *userData = nullptr, DataChunkVersionType maxVersion = DATA_CHUNK_VERSION_ANY );

	// BFME: highest version of this chunk in the BFME1 map scan, or DATA_CHUNK_VERSION_ANY when
	// this port has no ceiling for that name (an unregistered chunk is skipped and logged).
	static DataChunkVersionType mapChunkVersionCeiling(const char* label);

	Bool parse( void *userData = nullptr );						// parse the chunk stream using registered parsers
																									// assumed to be at the start of chunk when called
																									// can be called recursively

	Bool isValidFileType();											///< Returns TRUE if it is our file format.
	AsciiString openDataChunk(DataChunkVersionType *ver );
	void closeDataChunk();										// close chunk and move to start of next chunk

	Bool atEndOfFile() { return (m_file->eof()) ? true : false; }					// return true if at end of file
	// BFME: file position. A SidesList failure records each side's offset in the same
	// units as BfmeLastChunkErrorOffset.
	UnsignedInt tell() { return m_file->tell(); }
	// BFME: copy up to len bytes at the current position, then seek back. dataLeft
	// stays put, so the error offset recorded after a rethrow is unchanged.
	Int peekFileBytes(void *ptr, Int len)
	{
		const UnsignedInt pos = m_file->tell();
		const Int got = m_file->read(ptr, len);
		m_file->absoluteSeek(pos);
		return got;
	}
	Bool atEndOfChunk();											// return true if all data has been read from this chunk

	void reset();															// reset to just-opened state

	AsciiString getChunkLabel();							// return label of current data chunk
	DataChunkVersionType getChunkVersion();		// return version of current data chunk
	unsigned int getChunkDataSize();					// return size of data stored in this chunk
	unsigned int getChunkDataSizeLeft();			// return size of data left to read in this chunk


	Real readReal();
	Int readInt();
	Byte readByte();

	AsciiString readAsciiString();
	UnicodeString readUnicodeString();
	Dict readDict();
	void readArrayOfBytes(char *ptr, Int len);

	NameKeyType readNameKey();
};

// BFME: E1-52 innermost chunk whose parser threw or returned false, and the file
// position at that moment. The first site wins; a later parent parse does not replace it.
void BfmeResetChunkErrorSite();
const char *BfmeLastChunkErrorName();
UnsignedInt BfmeLastChunkErrorOffset();
