#pragma once

namespace melton {

// Registers the "Melton Clean Feed" video source. Add it to any scene (ideally a dedicated
// output scene that is never the Program) to use the clean feed anywhere OBS can send a
// source or scene: Fullscreen/Windowed Projector, Virtual Camera (Source/Scene), NDI, etc.
void RegisterCleanFeedSource();

} // namespace melton
