graph TB
    subgraph GameEngine["Game Engine (C)"]
        ER[engine_redraw.c\nredraw_view / draw_view]
        MF[map_fade_in/out\nPVM_ParchFadeIn/Out]
        GP[gui_parchment.c\nredraw_minimal_overhead_view]
    end

    subgraph Globals["Shared Globals"]
        LP[lbPalette\nunsigned char 768]
        SW[MyScreenWidth\nMyScreenHeight]
        VW[vec_window_width/height\nvert/hori_offset\nx/y_init_off]
    end

    subgraph RendererManager["RendererManager.cpp — C API boundary"]
        RM_LK[RendererLockScreen]
        RM_BF[RendererBeginFrame]
        RM_PF[RendererPresentFrame → EndFrame]
        RM_MF[MapFadePass_StepFadeIn/Out]
        RM_NT[RendererNotifyGameTablesReady]
        RM_PS[RendererPaletteSet → LbPaletteSet]
    end

    subgraph IRenderer["IRenderer interface"]
        BF[BeginFrame\npushes size+palette\nto all sub-renderers]
        EF[EndFrame\ncomposites final frame]
        ROGL[RendererOpenGL]
        RSW[RendererSoftware]
    end

    subgraph SubRenderers["Sub-Renderer Interfaces + GL Implementations"]
        direction TB

        subgraph WVR["IWorldViewRenderer"]
            GLWVR[GLWorldViewRenderer\nBeginWorldPass / GPURenderNow\nTileAtlas + KeeperSprite batching]
            SWWVR[SoftwareWorldViewRenderer\nfallback]
        end

        subgraph UIR["IUIRenderer"]
            GLUIR[GLUIRenderer\nSubmitPanelSprite\nSubmitSolidBox\nSubmitSlabSelector\nDrawFront / DrawBack]
            SWUIR[SoftwareUIRenderer\nnoop]
        end

        subgraph TR["ITextRenderer"]
            GLTR[GLTextRenderer\nDrawTextResized\nFlushSegment\nFont atlas cache]
            SWTR[SoftwareTextRenderer\nfallback]
        end

        subgraph MFP["IMapFadePass"]
            GLMFP[GLMapFadePass\nCaptureAndUploadFrames\nStepFadeIn/Out\nRenderGPUComposePass]
            SWMFP[SoftwareMapFadePass\nmap_fade pixel wipe]
        end

        subgraph CL["ICursorLayer"]
            GLCL[GLCursorLayer\nSubmitKeeperHandSprite\nprocess_keeper_sprite_ex]
            SWCL[SWCursorLayer\nfallback]
        end
    end

    subgraph GPUResources["GPU Resources"]
        TA[GLTileAtlas\n256×tile RGBA8 array]
        SA[GLSpriteAtlas\nGUI + button sprites]
        FA[GLFontAtlas\nper-font glyph atlas]
        PT[PaletteTexture\n256×1 RGBA8]
        FT[FadeTexture\nrender_fade_tables]
    end

    subgraph EngineRender["engine_render.c — Save/Restore"]
        ESR[engine_save_render_state\nengine_restore_render_state\nvec_w/h, vert/hori_offset\nengine_window x/y/w/h]
    end

    %% Game → RendererManager
    ER -->|RendererLockScreen| RM_LK
    MF -->|MapFadePass_StepFadeIn/Out| RM_MF
    GP -->|draw_map_parchment\ndraw_2d_map| GLUIR

    %% RendererManager → IRenderer lifecycle
    RM_LK --> RM_BF --> BF
    RM_PF --> EF
    RM_MF --> GLMFP
    RM_NT -->|SetFadeTexture\nSetPaletteSource| GLUIR & GLWVR
    RM_PS -->|writes| LP

    %% BeginFrame pushes state
    BF -->|SetFullScreenSize\nSetPaletteSource| GLWVR
    BF -->|SetScreenDimensions\nSetPaletteSource| GLUIR
    BF -->|SetScreenSize| GLTR
    BF -->|SetScreenSize| GLMFP

    %% Globals flow
    SW -->|read once\nin BeginFrame| BF
    LP -->|address registered\nat init + BeginFrame| GLWVR & GLUIR
    VW <-->|save/restore\nduring FBO capture| ESR

    %% RendererOpenGL owns sub-renderers
    ROGL --- GLWVR & GLUIR & GLTR & GLMFP & GLCL

    %% GPU resources shared
    ROGL -->|owns| TA & SA & FA & PT & FT
    GLWVR -->|reads| TA & PT & FT
    GLUIR -->|reads| SA & PT & FT
    GLTR -->|reads| FA & PT
    GLMFP -->|calls| GLWVR & ESR

    %% Software path
    RSW --- SWWVR & SWUIR & SWTR & SWMFP & SWCL

    style ROGL fill:#1a6b3c,color:#fff
    style RSW fill:#555,color:#fff
    style GLWVR fill:#1a5c8a,color:#fff
    style GLUIR fill:#1a5c8a,color:#fff
    style GLTR fill:#1a5c8a,color:#fff
    style GLMFP fill:#1a5c8a,color:#fff
    style GLCL fill:#1a5c8a,color:#fff
    style SWWVR fill:#555,color:#aaa
    style SWUIR fill:#555,color:#aaa
    style SWTR fill:#555,color:#aaa
    style SWMFP fill:#555,color:#aaa
    style SWCL fill:#555,color:#aaa
    style Globals fill:#3d2b00,color:#ffd
    style GPUResources fill:#2b003d,color:#ddf
    style EngineRender fill:#3d0000,color:#fdd