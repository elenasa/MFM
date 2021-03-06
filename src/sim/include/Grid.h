/*                                              -*- mode:C++ -*-
  Grid.h Encapsulator for all MFM logic
  Copyright (C) 2014,2017-2018 The Regents of the University of New Mexico.  All rights reserved.
  Copyright (C) 2017-2018 Ackleyshack,LLC.  All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
  USA
*/

/**
  \file Grid.h Encapsulator for all MFM logic
  \author Trent R. Small
  \author David H. Ackley.
  \author Elena S. Ackley.
  \date (C) 2014,2017-2018 All rights reserved.
  \lgpl
 */
#ifndef GRID_H
#define GRID_H

#include "itype.h"
#include "SizedTile.h"
#include "ElementTable.h"
#include "Random.h"
#include "Sense.h"
#include "GridConfig.h"
#include "GridTransceiver.h"
#include "ElementRegistry.h"
#include "Logger.h"
#include "LineCountingByteSource.h"
#include <time.h>  /* For struct timespec, clock_gettime */

namespace MFM {

  /**
   * A two-dimensional grid of simulated Tiles.
   */
  template <class GC>
  class Grid
  {
  public:
    // Extract short type names
    typedef typename GC::EVENT_CONFIG EC;
    typedef typename EC::ATOM_CONFIG AC;
    typedef typename AC::ATOM_TYPE T;

    enum { MAX_TILES_SUPPORTED = 500 };  // Yeah right.  Used for sizing m_rgi

    enum { R = EC::EVENT_WINDOW_RADIUS};
    enum { TILE_WIDTH = GC::TILE_WIDTH};
    enum { TILE_HEIGHT = GC::TILE_HEIGHT};
    enum { EVENT_HISTORY_SIZE = GC::EVENT_HISTORY_SIZE};
    enum { OWNED_WIDTH = TILE_WIDTH - 2 * R }; // Duplicating the OWNED_SIDE computation in Tile.tcc!
    enum { OWNED_HEIGHT = TILE_HEIGHT - 2 * R }; // Duplicating the OWNED_SIDE computation in Tile.tcc!
    enum { MAX_LOCKS_OWNED_PER_TILE = 3}; //checkboard: E,SE,S  staggered: NE,E,SE

    typedef SizedTile<EC,TILE_WIDTH,TILE_HEIGHT,EVENT_HISTORY_SIZE> GridTile;

  private:
    Random m_random;

    u32 m_seed;

    void InitSeed();

    void InitDummyTiles();

    const u32 m_width, m_height;

    const GridLayoutPattern m_layout;

    SPoint m_lastEventTile;

    ElementTypeNumberMap<EC> m_elementTypeNumberMap;

    GridTile * const m_tiles;
    GridTile & _getTile(u32 x, u32 y) { return m_tiles[x*m_height + y]; }
    const GridTile & _getTile(u32 x, u32 y) const { return m_tiles[x*m_height + y]; }

    LonglivedLock * const m_intertileLocks;
    LonglivedLock & _getIntertileLock(u32 x, u32 y, u32 i) {
      return m_intertileLocks[(x*m_height + y) * MAX_LOCKS_OWNED_PER_TILE + i];
    }

    LonglivedLock & GetIntertileLockStaggered(u32 xtile, u32 ytile, Dir dir);
    LonglivedLock & GetIntertileLockCheckerboard(u32 xtile, u32 ytile, Dir dir);


    GridTile m_heroTile;    // Model for the actual m_tiles

    /**
       Get the long-lived lock controlling cache activity going in
       direction dir from the Tile at (xtile,ytile) in the Grid.
     */
    LonglivedLock & GetIntertileLock(u32 xtile, u32 ytile, Dir dir, bool isStaggered) ;

    struct TileDriver {
      enum State { PAUSED, ADVANCING, EXIT_REQUEST };
      Mutex m_stateLock;
      State m_state;
      SPoint m_loc;
      Grid* m_gridPtr;
      pthread_t m_threadId;
      GridTransceiver m_channels[4]; // 4: NE, E, SE, S == dir-Dirs::NORTHEAST
      TileDriver()
        : m_state(PAUSED)
        , m_loc(-1,-1)
        , m_gridPtr(0)
      { }

      ~TileDriver() {} //avoid inline error

      State GetState()
      {
        Mutex::ScopeLock lock(m_stateLock);
        return m_state;
      }

      void SetState(State newState)
      {
        Mutex::ScopeLock lock(m_stateLock);
        m_state = newState;
      }

      Tile<EC> & GetTile()
      {
        return m_gridPtr->GetTile(m_loc);
      }

    };

    TileDriver * const m_tileDrivers;
    TileDriver & _getTileDriver(u32 x, u32 y) {
      return m_tileDrivers[x*m_height + y];
    }

    bool m_threadsInitted;
    static void * TileDriverRunner(void *) ;

    bool m_backgroundRadiationEnabled; // shadows value pushed to tiles
    bool m_foregroundRadiationEnabled; // shadows value pushed to tiles

    ElementRegistry<EC> m_er;

    s32 m_xraySiteOdds;

    /**
     * A synchronized command sequence to the grid
     */
    struct TileDriverControl
    {
      virtual const char * GetName() = 0;

      virtual bool CheckPrecondition(TileDriver &) = 0;
      virtual void MakeRequest(TileDriver &) = 0;
      virtual bool CheckIfReady(TileDriver &) = 0;
      virtual void Execute(TileDriver &) = 0;

      /**
         Called once per op at the beginning
       */
      virtual void PreGridControl(Grid&) = 0;

      /**
         Called once per op at the end
       */
      virtual void PostGridControl(Grid&) = 0;
    };

    /**
     * Operations for synchronized grid pausing
     */
    struct PauseControl : public TileDriverControl
    {
      virtual const char * GetName()
      {
        return "Pause";
      }

      virtual bool CheckPrecondition(TileDriver & td)
      {
        Tile<EC> & tile = td.GetTile();
        return !tile.IsOff();
      }

      virtual void MakeRequest(TileDriver & td)
      {
        Tile<EC> & tile = td.GetTile();
        tile.RequestStatePassive();
      }
      virtual bool CheckIfReady(TileDriver & td)
      {
        Tile<EC> & tile = td.GetTile();
        return tile.IsPassive();
      }
      virtual void Execute(TileDriver & td)
      {
        Tile<EC> & tile = td.GetTile();
        tile.Pause();
      }

      virtual void PreGridControl(Grid& grid)
      {
      }

      virtual void PostGridControl(Grid& grid)
      {
        grid.SetGridRunning(false);
      }

    };

    /**
     * Operations for synchronized grid unpausing
     */
    struct RunControl : public TileDriverControl
    {

      virtual const char * GetName()
      {
        return "Unpause";
      }

      virtual bool CheckPrecondition(TileDriver & td)
      {
        Tile<EC> & tile = td.GetTile();
        return !tile.IsActive();
      }

      virtual void MakeRequest(TileDriver & td)
      {
        Tile<EC> & tile = td.GetTile();
        tile.NeedAtomRecount();
        tile.RequestStateActive();
      }
      virtual bool CheckIfReady(TileDriver & td)
      {
        Tile<EC> & tile = td.GetTile();
        return tile.IsActive();
      }
      virtual void Execute(TileDriver & td)
      {
        // We're already running; nothing to do
      }

      virtual void PreGridControl(Grid& grid)
      {
        grid.SetGridRunning(true);
      }

      virtual void PostGridControl(Grid& grid)
      {
      }

    };

    /**
     * Synchronized operations driver.
     */
    void DoTileDriverControl(TileDriverControl & tc);

  public:
    struct GridTouchEvent {
      SiteTouchType m_touchType;
      SPoint m_gridAtomCoord;
      GridTouchEvent()
        : m_touchType(TOUCH_TYPE_NONE)
        , m_gridAtomCoord(0,0)
      { }
    };

    void SenseTouchAround(const GridTouchEvent & gridTouchEvent) ;

  private:
    void SenseTouchAt(const SPoint gridCoord, SiteTouchType touch) ;

  public:

    UlamClassRegistry<EC> & GetUlamClassRegistry()
    {
      return m_heroTile.GetUlamClassRegistry();
    }

    const UlamClassRegistry<EC> & GetUlamClassRegistry() const
    {
      return m_heroTile.GetUlamClassRegistry();
    }

    s32 GetWarpFactor() const
    {
      return m_heroTile.GetWarpFactor();
    }

    void SetWarpFactor(s32 wf)
    {
      if (wf < 0 || wf > 10)
      {
        FAIL(ILLEGAL_ARGUMENT);
      }

      m_heroTile.SetWarpFactor(wf);
    }

    double GetAverageCacheRedundancy() const;
    void SetCacheRedundancy(u32 redundancyOddsType) ;

    void ReportGridStatus(Logger::Level level) ;

    Random& GetRandom() { return m_random; }

    friend class GridRenderer;

    void SetSeed(u32 seed);

    Grid(ElementRegistry<EC>& elts, u32 width, u32 height, GridLayoutPattern layout)
      : m_random()
      , m_seed(0)
      , m_width(width)
      , m_height(height)
      , m_layout(layout)
      , m_tiles(new GridTile[m_width * m_height] )
      , m_intertileLocks(new LonglivedLock[m_width * m_height * MAX_LOCKS_OWNED_PER_TILE])
      , m_tileDrivers(new TileDriver[m_width * m_height * MAX_LOCKS_OWNED_PER_TILE])
      , m_threadsInitted(false)
      , m_backgroundRadiationEnabled(false)
      , m_foregroundRadiationEnabled(false)
      , m_er(elts)
      , m_xraySiteOdds(100)
      , m_rgi(m_width * m_height)
    {
      //dummy tiles not set for iterator use!!! avoid illegal tile coord.
      //for (iterator_type i = begin(); i != end(); ++i)
      //	  LOG.Debug("Tile[%d][%d] @ %p", i.GetX(), i.GetY(), &(*i));
    }

    s32* GetXraySiteOddsPtr()
    {
      return &m_xraySiteOdds;
    }

    void Init();

    /**
       Init the 'TileDriver' threads.  This is done separately from
       (and should happen after) Init() so that we can use a Grid
       (e.g., for testing) without necessarily dealing with all the
       threading.
     */
    void InitThreads();

    /**
       Enable or disable the tiles and the transceivers.
     */
    void SetGridRunning(bool running) ;

    const Tile<EC> & Get00Tile() const {
      return _getTile(0,0);
    }

    const Element<EC> * LookupElement(u32 elementType) const
    {
      return Get00Tile().GetElementTable().Lookup(elementType);
    }

    const Element<EC> * LookupElementFromSymbol(const u8 * symbol) const
    {
      return Get00Tile().GetElementTable().Lookup(symbol);
    }

    Element<EC> * LookupElementFromSymbol(const u8 * symbol) 
    {
      // Safe. But, barf.
      return
        const_cast<Element<EC> *>(static_cast<const Grid<GC>*>(this)->LookupElementFromSymbol(symbol));
    }


    ElementRegistry<EC>& GetElementRegistry()
    {
      return m_er;
    }

    void Needed(Element<EC> & anElement)
    {
      anElement.AllocateType(m_elementTypeNumberMap);         // Force a type now
      m_er.RegisterElement(anElement);  // Make sure we're in here (How could we not?)

      for (iterator_type i = begin(); i != end(); ++i)
        i->RegisterElement(anElement);

      LOG.Message("Assigned type 0x%04x for %@",anElement.GetType(),&anElement.GetUUID());
    }

    void SetTileParameter(u32 key, s32 value)
    {
      m_heroTile.SetTileParameter(key, value);
      LOG.Message("Tile parameter key %d set to value %d", key, value);
    }

    RandomIterator<MAX_TILES_SUPPORTED> m_rgi;
    SPoint IteratorIndexToCoord(const u32 idx) const
    {
      return SPoint(idx % m_width, idx / m_width);
    }

    /**
     * A minimal iterator over the Tiles of a grid.  Access via Grid::begin().
     */
    template <typename ItemType, typename GridType>
    class MyIterator
    {
      GridType & g;
      s32 i;
      s32 j;
      s32 incnt;
    public:
      MyIterator(GridType & g, int i = 0, int j = 0) : g(g), i(i), j(j), incnt(0) { }

      bool operator!=(const MyIterator &m) const { return i != m.i || j != m.j; }
      void operator++()
      {
	incnt++;
	bool failed = false;
	do{
	  if (j < (s32) g.m_height)
	    {
	      i++;
	      if (i >= (s32) g.m_width)
		{
		  i = 0;
		  j++;
		}
	    }
	  else
	    failed = true;
	} while((!g.IsLegalTileIndex(SPoint(i,j)) || g._getTile(i,j).IsDummyTile()) && !failed);
	//MFM_API_ASSERT_STATE(g.IsLegalTileIndex(SPoint(i,j))); except for 'end'
      }

      int operator-(const MyIterator &m) const
      {
	if(!g.IsGridLayoutStaggered())
	  {
	    //old way for checkerboard layout
	    s32 rows = j-m.j;
	    s32 cols = i-m.i;
	    MFM_API_ASSERT_STATE( (rows*g.m_width + cols) == (incnt - m.incnt)); //sanity
	  }
        return incnt - m.incnt;
      }

      ItemType & operator*() const
      {
	MFM_API_ASSERT_ARG(g.IsLegalTileIndex(SPoint(i,j)));
        return g._getTile(i,j);
      }

      ItemType * operator->() const
      {
	MFM_API_ASSERT_ARG(g.IsLegalTileIndex(SPoint(i,j)));
        return &g._getTile(i,j);
      }

      SPoint At() const { return SPoint(i,j); }
      u32 GetX() const { return (u32) i; }
      u32 GetY() const { return (u32) j; }

    };

    typedef MyIterator< Tile<EC>, Grid<GC> > iterator_type;
    typedef MyIterator< const Tile<EC>, const Grid<GC> > const_iterator_type;

    iterator_type begin() { return iterator_type(*this); }

    const_iterator_type begin() const { return const_iterator_type(*this); }

    iterator_type end() { return iterator_type(*this,0,m_height); }

    const_iterator_type end() const { return const_iterator_type(*this, 0,m_height); }

    ~Grid()
    {
      delete [] m_tiles;
      delete [] m_intertileLocks;
      delete [] m_tileDrivers;
    }

    /**
     * Used to tell this Tile whether or not to actually execute any
     * events, or to just wait on any packet communication from other
     * Tiles instead.
     *
     * @param tileLoc The position of the Tile to set.
     *
     * @param value If \c value is \c true, this tells the Tile to begin
     *              executing its own events. Else, this Tile will
     *              only process Packets from other Tiles.
     */
    void SetTileEnabled(const SPoint& tileLoc, bool value);

    /**
     * Checks whether or not a specific Tile is currently executing
     * its own events or is simply processing Packets.
     *
     * @param tileLoc The location of the Tile in this Grid to check.
     */
    bool IsTileEnabled(const SPoint& tileLoc);

    /**
     * Clears a Tile of all of its held atoms.
     *
     * @param tileLoc The location of the Tile in this Grid to clear.
     */
    void EmptyTile(const SPoint& tileLoc)
    {
      Tile<EC> & tile = GetTile(tileLoc);
      tile.ClearAtoms();
    }

    /**
     * Empties this Grid of all held Atoms.
     */
    void Clear();

    /**
     * Based on the current connectivity pattern, check the visible
     * regions of each tile against the caches of its connected tiles.
     * Report debug messages for any discrepancies.
     */
    void CheckCaches();

    /**
     * Update all cache sites from their corresponding source,
     * 'non-physically'.  This is thread-unsafe and no tile driver
     * threads should be active, else races and inconsistencies are
     * likely.
     */
    void RefreshAllCaches();

    /**
     * Return true iff tileInGrid is a legal tile coordinate in this
     * grid, meaning it's in the range (0,0) to (tilesWide-1,
     * tilesHigh-1).  If this returns false, GetTile(tileInGrid) is
     * unsafe.
     */
    bool IsLegalTileIndex(const SPoint & tileInGrid) const;

    /**
     * Return true iff grid layout is staggered, and either tileInGridY
     * is the last column of an even row (X), or the first column of an odd row.
     * If this returns true, GetTile(tileInGrid) is unsafe.
     *
     * Called once (if at all!), only to init the tiles in the grid;
     * hence forth use Tile<EC>::IsDummyTile() for generality.
     */
    bool IsDummyTileCoord(s32 tileingridX, s32 tileingridY) const;

    /**
     * Reinitialize m_rgi for a staggered layout grid, s.t.
     * it holds the non-sequential dummy tile grid indexes; m_limit changes.
     */
    void ReinitGridRandomIterator();

    /**
     * Return true iff siteInGrid is a legal site coordinate in this
     * grid (not including the cache, like gridpanel).  If this
     * returns false, GetAtom(siteInGrid) and PlaceAtom(T, siteInGrid)
     * will FAIL.
     */
    bool IsGridCoord(const SPoint & siteInGrid) const;

    /**
     * Run an event at siteInGrid if the grid is paused and siteInGrid
     * is legal, and return true.  Return false if siteInGrid is
     * illegal or the grid is not paused.
     */
    bool RunEventIfPausedAt(const SPoint & siteInGrid);

    /**
     * Find the grid coordinate of the 'owning tile' (i.e., ignoring
     * caches) for the give siteInGrid.  Return false if there isn't
     * one, otherwise set tileInGrid and siteInTile appropriately and
     * return true.
     *
     * Note that although siteInGrid is specified in 'uncached'
     * coordinates (in which (0,0) is the upperleftmost uncached
     * location of the tile at (0,0)), siteInTile is returned in
     * 'including cache' coordinates (in which (R,R) is the
     * upperleftmost uncached location of the tile at (0,0).  See
     * MapGridToUncachedTile for an alternative.
     */
    bool MapGridToTile(const SPoint & siteInGrid, SPoint & tileInGrid, SPoint & siteInTile) const;

    /**
     * Find the grid coordinate of the 'owning tile' (i.e., ignoring
     * caches) for the given siteInGrid.  Return false if there isn't
     * one, otherwise set tileInGrid and siteInTile appropriately and
     * return true.
     */
    bool MapGridToUncachedTile(const SPoint & siteInGrid, SPoint & tileInGrid, SPoint & siteInTile) const;

    /**
     * Return the Grid height in Tiles
     */
    u32 GetHeight() const { return m_height; }

    /**
     * Return the Grid width in Tiles
     */
    u32 GetWidth() const { return m_width; }

    /**
     * Return the Grid height in (non-cache) sites
     */
    u32 GetHeightSites() const
    {
      return GetHeight() * OWNED_HEIGHT;
    }

    /**
     * Return the Grid width in (non-cache) sites;
     *        compensated by half owned_width per row for staggered grid layout
     */
    u32 GetWidthSites() const
    {
      if(IsGridLayoutStaggered())
	return GetWidth() * OWNED_WIDTH + OWNED_WIDTH/2;
      return GetWidth() * OWNED_WIDTH;
    }

    /**
     * Return the Grid layout
     */
    GridLayoutPattern GetGridLayout() const
    {
      return m_layout;
    }

    /**
     * Return true when the Grid layout is staggered
     */
    bool IsGridLayoutStaggered() const
    {
      return (m_layout == GRID_LAYOUT_STAGGERED);
    }

    /**
     * Return true when the Grid layout is staggered, and tile row of site is odd
     */
    bool IsGridRowStaggered(const SPoint & siteInGrid) const
    {
      return IsGridLayoutStaggered() && ((siteInGrid.GetY()/OWNED_HEIGHT)%2 > 0);
    }

    /**
     * Shut down all tile threads
     */
    void ShutdownTileThreads()
    {
      LOG.Message("Sending exit requests to the tiles");
      for (iterator_type i = begin(); i != end(); ++i)
      {
        TileDriver & td = _getTileDriver(i.GetX(),i.GetY());
        td.SetState(TileDriver::EXIT_REQUEST);
      }
      SleepMsec(500);
    }

    /**
     * Synchronize and pause the entire grid
     */
    void Pause()
    {
      PauseControl pc;
      DoTileDriverControl(pc);
    }

    /**
     * Synchronize and unpause the entire grid
     */
    void Unpause()
    {
      RunControl rc;
      DoTileDriverControl(rc);
    }

    /**
     * Resets all atom counts and refreshes the atoms counts in
     * every tile in the grid.
     */
    void RecountAtoms();

    void CheckAtom(const T& atom, const SPoint& location);

    void PlaceAtom(const T& atom, const SPoint& location);

    void PlaceAtomInSite(bool placeInBase, const T& atom, const SPoint& location, bool checkOnly=false);

    void XRayAtom(const SPoint& location);

    void MaybeXRayAtom(const SPoint& location);

    void SaveSite(const SPoint& loc, ByteSink& bs, AtomTypeFormatter<AC> & atf) const
    {
      if(!IsGridCoord(loc)) return;

      SPoint tileInGrid, siteInTile;
      if (!MapGridToTile(loc, tileInGrid, siteInTile))
      {
        LOG.Error("Site (%d,%d) does not map to grid. Site not saved.",
                  loc.GetX(), loc.GetY());
        FAIL(ILLEGAL_ARGUMENT);
      }
      GetTile(tileInGrid).SaveSite(siteInTile,bs,atf);
    }

    bool LoadSite(const SPoint& loc, LineCountingByteSource& bs, AtomTypeFormatter<AC> & atf)
    {
      if(!IsGridCoord(loc)) return false;
      SPoint tileInGrid, siteInTile;
      if (!MapGridToTile(loc, tileInGrid, siteInTile))
      {
        LOG.Error("Site (%d,%d) does not map to grid. Site not loaded.",
                  loc.GetX(), loc.GetY());
        FAIL(ILLEGAL_ARGUMENT);
      }
      return GetTile(tileInGrid).LoadSite(siteInTile,bs,atf);
    }

    const T* GetAtom(SPoint& loc)
    {
      return GetAtomInSite(false, loc);
    }

    const T* GetAtomInSite(bool getFromBase, SPoint& siteInGrid)
    {
      SPoint tileInGrid, siteInTile;
      if (!MapGridToTile(siteInGrid, tileInGrid, siteInTile))
      {
        LOG.Error("Can't get atom at site (%d,%d): Does not map to grid.",
                  siteInGrid.GetX(), siteInGrid.GetY());
        FAIL(ILLEGAL_ARGUMENT);  // XXX Change to return bool?
      }
      return GetTile(tileInGrid).GetAtomInSite(getFromBase, siteInTile);
    }

    T* GetWritableAtom(SPoint& loc)
    {
      SPoint tileInGrid, siteInTile;
      if (!MapGridToTile(loc, tileInGrid, siteInTile))
      {
        return NULL;
      }
      return GetTile(tileInGrid).GetWritableAtom(siteInTile);
    }

    void FillLastEventTile(SPoint& out);

    inline Tile<EC> & GetTile(const SPoint& pt)
    {
      if (!IsLegalTileIndex(pt))
      {
        LOG.Error("Can't get tile at (%d,%d); illegal tile index.",
                  pt.GetX(), pt.GetY());
        FAIL(ILLEGAL_ARGUMENT);
      }
      return GetTile(pt.GetX(), pt.GetY());
    }

    inline const Tile<EC> & GetTile(const SPoint& pt) const
    {
      if (!IsLegalTileIndex(pt))
      {
        LOG.Error("Can't get const tile at (%d,%d); illegal tile index.",
                  pt.GetX(), pt.GetY());
        FAIL(ILLEGAL_ARGUMENT);
      }
      return GetTile(pt.GetX(), pt.GetY());
    }

    inline Tile<EC> & GetTile(u32 x, u32 y)
    { return _getTile(x,y); }

    inline const Tile<EC> & GetTile(u32 x, u32 y) const
    { return _getTile(x,y); }

    /* Don't count caches! Don't count staggered undef ends!! */
    inline const u32 GetTotalSites()
    { return GetWidth() * OWNED_WIDTH * GetHeightSites(); }

    u64 GetTotalEventsExecuted() const;

    u64 GetTotalSitesAccessed() const;

    void WriteEPSImage(ByteSink & outstrm) const;

    void WriteEPSAverageImage(ByteSink & outstrm) const;

    void ResetEPSCounts();

    u32 GetAtomCount(ElementType atomType) const;

    s32 GetAtomCountFromSymbol(const u8 * elementSymbol) const;

    /**
     * Counts the number of sites which are occupied in this Grid and
     * gets a percentage, in the range [0.0 , 1.0] , describing the
     * sites which are occupied.
     *
     * @returns a percentage of the occupied sites in this Grid .
     */
    double GetFullSitePercentage() const
    {
      // Avoid using GetWidthSites, because it includes missing sites if the grid is staggered.
      return 1.0 - ((double)GetAtomCount(Element_Empty<EC>::THE_INSTANCE.GetType()) /
                    (double)(GetHeight() * OWNED_HEIGHT * GetWidth() * OWNED_WIDTH));
    }

    //    void SurroundRectangleWithWall(s32 x, s32 y, s32 w, s32 h, s32 thickness);

    /**
     * Picks a random site in the grid, then sets all sites in a
     * randomly sized radius to Element_Empty .
     */
    void RandomNuke();

    /**
     * Sets whether or not background radiation will begin mutating
     * the Atoms of this Grid upon writing.
     */
    void SetBackgroundRadiationEnabled(bool value);

    /**
     * Checks to see if this Grid is currently administering
     * background radiation.
     */
    bool IsBackgroundRadiationEnabled() const
    {
      return m_backgroundRadiationEnabled;
    }


    /**
     * Sets whether or not foreground radiation will begin mutating
     * the Atoms of this Grid upon reading.
     */
    void SetForegroundRadiationEnabled(bool value);

    /**
     * Checks to see if this Grid is currently administering
     * foreground radiation.
     */
    bool IsForegroundRadiationEnabled() const
    {
      return m_foregroundRadiationEnabled;
    }

    /**
     * Randomly flips bits in randomly selected sites in this grid.
     */
    void XRay();

    /**
     * Randomly erases randomly selected sites in this grid.
     */
    void Thin();

    u32 CountActiveSites() const;
  };
} /* namespace MFM */

#include "Grid.tcc"

#endif /*GRID_H*/
