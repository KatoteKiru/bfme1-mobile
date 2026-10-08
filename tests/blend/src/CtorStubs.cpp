// BFME1 Mobile — tests (E1-25 review #10)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Link stubs for what WorldHeightMap(ChunkInputStream*, Bool) references in addition to the chunk
// parsers the other tests call directly (the linker keeps the whole constructor):
// - logicalDataOnly: PolygonTrigger::deleteTriggers() and SidesList::emptySides() run before the parse
//   (no-ops here: nothing was loaded); the WorldInfo / ObjectsList / PolygonTriggers / SidesList parsers
//   are registered but the synthetic maps have no such chunks (std::abort if reached);
// - GlobalLighting (ZH maps): TheW3DShadowManager is nullptr, the parser skips the shadow colour.
#include "PreRTS.h"

#include "Common/GameCommon.h"
#include "Common/NameKeyGenerator.h"
#include "Common/ThingFactory.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/SidesList.h"

#include <cstdlib>

class W3DShadowManager;
W3DShadowManager* TheW3DShadowManager = nullptr;

void PolygonTrigger::deleteTriggers() {}
void SidesList::emptySides() {}

Bool PolygonTrigger::ParsePolygonTriggersDataChunk(DataChunkInput&, DataChunkInfo*, void*) { std::abort(); }
Bool SidesList::ParseSidesDataChunk(DataChunkInput&, DataChunkInfo*, void*) { std::abort(); }

// reached only from the WorldInfo / ObjectsList parsers and DataChunkInput::readDict
NameKeyGenerator* TheNameKeyGenerator = nullptr;
NameKeyType NameKeyGenerator::nameToKey(const AsciiString&) { std::abort(); }
NameKeyType StaticNameKey::key() const { std::abort(); }
ThingFactory* TheThingFactory = nullptr;
ThingTemplate* ThingFactory::findTemplateInternal(const AsciiString&, Bool) { std::abort(); }
Real normalizeAngle(Real) { std::abort(); }
