#include <fbxppch.h>
#include <fbxpstate.h>
#include <fbxpnorm.h>

/**
 * Helper function to calculate tangents when the tangent element layer is missing.
 * Can be used in multiple threads.
 * http://gamedev.stackexchange.com/a/68617/39505
 **/
template < typename TVertex >
void CalculateTangents( TVertex* vertices, size_t vertexCount ) {
    std::vector< mathfu::vec3 > tan;
    tan.resize( vertexCount * 2 );
    memset( tan.data( ), 0, sizeof( mathfu::vec3 ) * vertexCount * 2 );

    auto tan1 = tan.data( );
    auto tan2 = tan1 + vertexCount;

    for ( size_t i = 0; i < vertexCount; i += 3 ) {
        const TVertex v0 = vertices[ i + 0 ];
        const TVertex v1 = vertices[ i + 1 ];
        const TVertex v2 = vertices[ i + 2 ];

        const float x1 = v1.position[ 0 ] - v0.position[ 0 ];
        const float x2 = v2.position[ 0 ] - v0.position[ 0 ];
        const float y1 = v1.position[ 1 ] - v0.position[ 1 ];
        const float y2 = v2.position[ 1 ] - v0.position[ 1 ];
        const float z1 = v1.position[ 2 ] - v0.position[ 2 ];
        const float z2 = v2.position[ 2 ] - v0.position[ 2 ];

        const float s1 = v1.texCoords[ 0 ] - v0.texCoords[ 0 ];
        const float s2 = v2.texCoords[ 0 ] - v0.texCoords[ 0 ];
        const float t1 = v1.texCoords[ 1 ] - v0.texCoords[ 1 ];
        const float t2 = v2.texCoords[ 1 ] - v0.texCoords[ 1 ];

        const float r = 1.0f / ( s1 * t2 - s2 * t1 );
        const mathfu::vec3 sdir( ( t2 * x1 - t1 * x2 ) * r, ( t2 * y1 - t1 * y2 ) * r, ( t2 * z1 - t1 * z2 ) * r );
        const mathfu::vec3 tdir( ( s1 * x2 - s2 * x1 ) * r, ( s1 * y2 - s2 * y1 ) * r, ( s1 * z2 - s2 * z1 ) * r );

        // assert( !isnan( sdir.x ) && !isnan( sdir.y ) && !isnan( sdir.z ) );
        // assert( !isnan( tdir.x ) && !isnan( tdir.y ) && !isnan( tdir.z ) );

        tan1[ i + 0 ] += sdir;
        tan1[ i + 1 ] += sdir;
        tan1[ i + 2 ] += sdir;

        tan2[ i + 0 ] += tdir;
        tan2[ i + 1 ] += tdir;
        tan2[ i + 2 ] += tdir;
    }

    for ( size_t i = 0; i < vertexCount; i += 1 ) {
        TVertex& v = vertices[ i ];
        const auto n = mathfu::vec3( v.normal[ 0 ], v.normal[ 1 ], v.normal[ 2 ] );
        const auto t = tan1[ i ];

        mathfu::vec3 tt = mathfu::normalize( t - n * mathfu::dot( n, t ) );
        // assert( !isnan( tt.x ) && !isnan( tt.y ) && !isnan( tt.z ) );

        v.tangent[ 0 ]  = tt.x;
        v.tangent[ 1 ]  = tt.y;
        v.tangent[ 2 ]  = tt.z;
        v.tangent[ 3 ]  = ( mathfu::dot( mathfu::cross( n, t ), tan2[ i ] ) < 0 ) ? -1.0f : 1.0f;
    }
}

/**
 * Calculate normals for the faces (does not weight triangles).
 * Fast and usable results, however incorrect.
 * TODO: Implement vertex normal calculations.
 **/
template < typename TVertex >
void CalculateFaceNormals( TVertex* vertices, size_t vertexCount ) {
    for ( size_t i = 0; i < vertexCount; i += 3 ) {
        TVertex& v0 = vertices[ i + 0 ];
        TVertex& v1 = vertices[ i + 1 ];
        TVertex& v2 = vertices[ i + 2 ];

        const mathfu::vec3 p0( v0.position );
        const mathfu::vec3 p1( v1.position );
        const mathfu::vec3 p2( v2.position );
        const mathfu::vec3 n( mathfu::normalize( mathfu::cross( p1 - p0, p2 - p0 ) ) );

        v0.normal[ 0 ] = n.x;
        v0.normal[ 1 ] = n.y;
        v0.normal[ 2 ] = n.z;

        v1.normal[ 0 ] = n.x;
        v1.normal[ 1 ] = n.y;
        v1.normal[ 2 ] = n.z;

        v2.normal[ 0 ] = n.x;
        v2.normal[ 1 ] = n.y;
        v2.normal[ 2 ] = n.z;
    }
}

/**
 * Produces mesh subsets and subset indices.
 * A subset is a structure for mapping material index to a polygon range to allow a single mesh to
 * be rendered using multiple materials.
 * The usage could be: 1) render polygon range [ 0, 12] with 1st material.
 *                     2) render polygon range [12, 64] with 2nd material.
 *                     * range is [base index; index count]
 *
 * @param indices The indices of the mesh that will be used to draw the mesh with multiple materials.
 * @param subsets The ranges of the vertex indices for each material of the node.
 * @param subsetPolies A mapping of material indices to polygon ranges (useful for knowing the basic structure).
 * @return True on success.
 **/
template < typename TIndex >
bool GetSubsets( FbxMesh*                           mesh,
                 apemode::Mesh&                        m,
                 std::vector< TIndex >&             indices,
                 std::vector< apemodefb::SubsetFb >& subsets,
                 std::vector< apemodefb::SubsetFb >& subsetPolies,
                 std::vector< std::tuple< TIndex, TIndex > >& items ) {
    auto& s = apemode::Get( );

    s.console->info("Mesh \"{}\" has {} material(s) assigned.", mesh->GetNode( )->GetName( ), mesh->GetNode( )->GetMaterialCount( ) );

    // No submeshes for a node that has only 1 or no materials.
    if (mesh->GetNode()->GetMaterialCount() < 2) {
        return false;
    }

    //
    // Print materials attached to a node.
    //

    for ( auto k = 0; k < mesh->GetNode( )->GetMaterialCount( ); ++k ) {
        s.console->info( "\t#{} - \"{}\".", k, mesh->GetNode( )->GetMaterial( k )->GetName( ) );
    }

    items.clear( );
    indices.clear( );
    subsets.clear( );
    subsetPolies.clear( );

    items.reserve( mesh->GetPolygonCount( ) );
    indices.reserve( mesh->GetPolygonCount( ) * 3 );
    subsets.reserve( mesh->GetNode( )->GetMaterialCount( ) );
    subsetPolies.reserve( mesh->GetNode( )->GetMaterialCount( ) );

    // Go though all the material elements and map them.
    if ( const uint32_t mc = (uint32_t) mesh->GetElementMaterialCount( ) ) {
        s.console->info( "Mesh \"{}\" has {} material elements.", mesh->GetNode( )->GetName( ), mc );

        for ( uint32_t m = 0; m < mc; ++m ) {
            if ( const auto materialElement = mesh->GetElementMaterial( m ) ) {
                // The only mapping mode for materials that makes sense is polygon mapping.
                const auto materialMappingMode = materialElement->GetMappingMode( );

                // Handle the case with splitted meshes.
                if (materialMappingMode == FbxLayerElement::eAllSame)
                    continue;

                if ( materialMappingMode != FbxLayerElement::eByPolygon ) {
                    s.console->error( "Material element #{} has {} mapping mode (not supported).", m, materialMappingMode );
                    DebugBreak( );
                    continue;
                }

                // Mapping is done though the polygon indices.
                // For each polygon we have assigned material index.
                const auto& materialIndices = materialElement->GetIndexArray( );
                if (materialIndices.GetCount() < 1) {
                    s.console->error( "Material element {} has no indices, skipped.", m );
                    DebugBreak( );
                    continue;
                }

                const uint32_t pc = (uint32_t) mesh->GetPolygonCount( );
                for ( uint32_t i = 0; i < pc; ++i )
                    items.emplace_back( (TIndex) materialIndices.GetAt( i ), i );
            }
        }
    }

    if ( items.empty( ) ) {
        s.console->error( "Mesh \"{}\" has no correctly mapped materials (fallback to first one).", mesh->GetNode( )->GetName( ) );
        // Splitted meshes per material case, do not issues a debug break.
        // DebugBreak( );
        return false;
    }

    //
    // The most important part:
    // 1) Make sure our mapping is sorted, the FBX SDK doc does not state their data is sorted
    //    but in practice I did not see the unsorted polygon indices.
    // 2) Fill all the polygon ranges (consider breaks in the sorted ranges).
    //    If the polygon indices are [2, 3, 4, 10, 11, 12, 13, 15, 17]
    //    we will get {2, 3} (range starts at 2 and is 3 polygons long),
    //                {10, 4}, {15, 1}, {17, 1}.
    //

    using U = std::tuple< TIndex, TIndex >;
    auto sortByMaterialIndex = [&]( const U& a, const U& b ) { return std::get< 0 >( a ) < std::get< 0 >( b ); };
    auto sortByPolygonIndex  = [&]( const U& a, const U& b ) { return std::get< 1 >( a ) < std::get< 1 >( b ); };

    // Sort items by material index.
    std::sort( items.begin( ), items.end( ), sortByMaterialIndex );

    auto extractSubsetsFromRange = [&]( const uint32_t mii, const uint32_t ii, const uint32_t i ) {
        uint32_t ki = ii;
        TIndex k  = std::get< 1 >( items[ ii ] );

        uint32_t j = ii;
        for ( ; j < i; ++j ) {
            //indices.push_back( j * 3 + 0 );
            //indices.push_back( j * 3 + 0 );
            //indices.push_back( j * 3 + 2 );

            const TIndex kk  = std::get< 1 >( items[ j ] );
            if ((kk - k) > 1) {
                // s.console->info( "Break in {} - {} vs {}", j, k, kk );
                // Process a case where there is a break in polygon indices.
                s.console->info( "\tAdding subset: material #{}, polygon #{}, count {} ({} - {}).", mii, k, j - ki, ki, j - 1 );

                // subsetPolies.emplace_back( mii, k, j - ki );
                subsetPolies.emplace_back( mii, ki, j - ki );
                // subsets.emplace_back( mii, ki, j - ki );
                // subsets.emplace_back( mii, ki * 3, ( j - ki ) * 3 );
                ki = j;
                k  = kk;
            }

            k = kk;
        }

        s.console->info( "\tAdding subset: material #{}, polygon #{}, count {} ({} - {}).", mii, k, j - ki, ki, j - 1 );

        // subsetPolies.emplace_back( mii, k, j - ki );
        subsetPolies.emplace_back( mii, ki, j - ki );
        // subsets.emplace_back( mii, ki, j - ki );
        // subsets.emplace_back( mii, ki * 3, ( j - ki ) * 3 );
    };

    uint32_t ii = 0;
    TIndex mii = std::get< 0 >( items.front( ) );

    uint32_t i  = 0;
    const uint32_t ic = (uint32_t) items.size( );
    for ( ; i < ic; ++i ) {
        const TIndex mi = std::get< 0 >( items[ i ] );
        if ( mi != mii ) {
            s.console->info( "Material #{} has {} assigned polygons ({} - {}).", mii, i - ii, ii, i - 1 );

            // Sort items by polygon index.
            std::sort( items.data( ) + ii, items.data( ) + i, sortByPolygonIndex );
            extractSubsetsFromRange( mii, ii, i );
            ii  = i;
            mii = mi;
        }
    }

    s.console->info( "Material #{} has {} assigned polygons ({} - {}).", mii, i - ii, ii, i - 1 );
    std::sort( items.data( ) + ii, items.data( ) + i, sortByPolygonIndex );
    extractSubsetsFromRange( mii, ii, i );

    TIndex subsetStartIndex = 0;
    uint32_t materialIndex = (uint32_t) -1;

    if ( !subsetPolies.empty( ) ) {
        s.console->info( "Mesh index subsets from {} polygon ranges:", subsetPolies.size( ) );
    }

    for ( auto& sp : subsetPolies ) {
        if ( materialIndex != sp.material_id( ) ) {
            const TIndex indexCount = (TIndex) indices.size( );

            if ( materialIndex != (uint32_t) -1 ) {
                const auto subsetLength = indexCount - subsetStartIndex;
                subsets.emplace_back( materialIndex, (uint32_t)subsetStartIndex, (uint32_t)subsetLength );

                s.console->info( "\tMesh subset #{} for material #{} index range: [{}; {}].",
                                 subsets.size( ) - 1,
                                 materialIndex,
                                 subsetStartIndex,
                                 subsetLength );
            }

            materialIndex    = sp.material_id( );
            subsetStartIndex = indexCount;
        }

        for ( uint32_t pp = 0; pp < sp.index_count( ); ++pp ) {
            const TIndex pii = ( TIndex )( sp.base_index( ) + pp );
            indices.push_back( pii * 3 + 0 );
            indices.push_back( pii * 3 + 1 );
            indices.push_back( pii * 3 + 2 );
        }
    }

    if ( !subsetPolies.empty( ) ) {
        const TIndex indexCount   = (TIndex) indices.size( );
        const auto subsetLength = indexCount - subsetStartIndex;
        subsets.emplace_back( materialIndex, subsetStartIndex, subsetLength );

        s.console->info( "\tMesh subset #{} for material #{} index range: [{}; {}].",
                         subsets.size( ) - 1,
                         materialIndex,
                         subsetStartIndex,
                         subsetLength );
    }

    assert( indices.size( ) == ( size_t )( mesh->GetPolygonCount( ) * 3 ) );
    assert( subsets.size( ) <= ( size_t )( mesh->GetNode( )->GetMaterialCount( ) ) );
    return true;
}

//
// Helper function to get values from geometry element layers
// with their reference and mapping modes.
//

/**
 * Returns the value from the element layer by index with respect to reference mode.
 **/
template < typename TElementLayer, typename TElementValue >
TElementValue GetElementValue( const TElementLayer* elementLayer, uint32_t i ) {
    assert( elementLayer );
    switch ( const auto referenceMode = elementLayer->GetReferenceMode( ) ) {
        case FbxLayerElement::EReferenceMode::eDirect:
            return elementLayer->GetDirectArray( ).GetAt( i );
        case FbxLayerElement::EReferenceMode::eIndex:
        case FbxLayerElement::EReferenceMode::eIndexToDirect: {
            const int j = elementLayer->GetIndexArray( ).GetAt( i );
            return elementLayer->GetDirectArray( ).GetAt( j );
        }

        default:
            apemode::Get( ).console->error(
                "Reference mode {} of layer \"{}\" "
                "is not supported.",
                referenceMode,
                elementLayer->GetName( ) );

            DebugBreak( );
            return TElementValue( );
    }
}

/**
 * Returns the value from the element layer with respect to reference and mapping modes.
 **/
template < typename TElementLayer, typename TElementValue >
TElementValue GetElementValue( const TElementLayer* elementLayer,
                               uint32_t             controlPointIndex,
                               uint32_t             vertexIndex,
                               uint32_t             polygonIndex ) {
    if ( nullptr == elementLayer )
        return TElementValue( );

    switch ( const auto mappingMode = elementLayer->GetMappingMode( ) ) {
        case FbxLayerElement::EMappingMode::eByControlPoint:
            return GetElementValue< TElementLayer, TElementValue >( elementLayer, controlPointIndex );
        case FbxLayerElement::EMappingMode::eByPolygon:
            return GetElementValue< TElementLayer, TElementValue >( elementLayer, polygonIndex );
        case FbxLayerElement::EMappingMode::eByPolygonVertex:
            return GetElementValue< TElementLayer, TElementValue >( elementLayer, vertexIndex );

        default:
            apemode::Get( ).console->error(
                "Mapping mode {} of layer \"{}\" "
                "is not supported.",
                mappingMode,
                elementLayer->GetName( ) );

            DebugBreak( );
            return TElementValue( );
    }
}

/**
 * Returns nullptr in case element layer has unsupported properties or is null.
 **/
template < typename TElementLayer >
const TElementLayer* VerifyElementLayer( const TElementLayer* elementLayer ) {
    if ( nullptr == elementLayer ) {
        apemode::Get( ).console->error( "Missing element layer." );
        return nullptr;
    }

    switch ( const auto mappingMode = elementLayer->GetMappingMode( ) ) {
        case FbxLayerElement::EMappingMode::eByControlPoint:
        case FbxLayerElement::EMappingMode::eByPolygon:
        case FbxLayerElement::EMappingMode::eByPolygonVertex:
            break;
        default:
            apemode::Get( ).console->error(
                "Mapping mode {} of layer \"{}\" "
                "is not supported.",
                mappingMode,
                elementLayer->GetName( ) );
            return nullptr;
    }

    switch ( const auto referenceMode = elementLayer->GetReferenceMode( ) ) {
        case FbxLayerElement::EReferenceMode::eDirect:
        case FbxLayerElement::EReferenceMode::eIndex:
        case FbxLayerElement::EReferenceMode::eIndexToDirect:
            break;
        default:
            apemode::Get( ).console->error(
                "Reference mode {} of layer \"{}\" "
                "is not supported.",
                referenceMode,
                elementLayer->GetName( ) );
            return nullptr;
    }

    return elementLayer;
}

/**
 * Helper structure to assign vertex property values
 **/
struct StaticVertex {
    float position[ 3 ];
    float normal[ 3 ];
    float tangent[ 4 ];
    float texCoords[ 2 ];
};

//
// Flatbuffers takes care about correct platform-independent alignment.
//

static_assert( sizeof( StaticVertex ) == sizeof( apemodefb::StaticVertexFb ), "Must match" );
static_assert( sizeof( apemodefb::PackedVertexFb ) == sizeof( apemodefb::PackedVertexFb ), "Must match" );

/**
 * Initialize vertices with very basic properties like 'position', 'normal', 'tangent', 'texCoords'.
 * Calculate mesh position and texcoord min max values.
 **/
template < typename TVertex >
void InitializeVertices( FbxMesh*      mesh,
                         apemode::Mesh&   m,
                         TVertex*      vertices,
                         size_t        vertexCount,
                         mathfu::vec3& positionMin,
                         mathfu::vec3& positionMax,
                         mathfu::vec2& texcoordMin,
                         mathfu::vec2& texcoordMax ) {
    auto& s = apemode::Get( );
    const uint32_t cc = (uint32_t) mesh->GetControlPointsCount( );
    const uint32_t pc = (uint32_t) mesh->GetPolygonCount( );

    s.console->info( "Mesh \"{}\" has {} control points.", mesh->GetNode( )->GetName( ), cc );
    s.console->info( "Mesh \"{}\" has {} polygons.", mesh->GetNode( )->GetName( ), pc );

    positionMin.x = std::numeric_limits< float >::max( );
    positionMin.y = std::numeric_limits< float >::max( );
    positionMin.z = std::numeric_limits< float >::max( );
    positionMax.x = std::numeric_limits< float >::min( );
    positionMax.y = std::numeric_limits< float >::min( );
    positionMax.z = std::numeric_limits< float >::min( );

    texcoordMin.x = std::numeric_limits< float >::max( );
    texcoordMin.y = std::numeric_limits< float >::max( );
    texcoordMax.x = std::numeric_limits< float >::min( );
    texcoordMax.y = std::numeric_limits< float >::min( );

    const auto uve = VerifyElementLayer( mesh->GetElementUV( ) );
    const auto ne  = VerifyElementLayer( mesh->GetElementNormal( ) );
    const auto te  = VerifyElementLayer( mesh->GetElementTangent( ) );

    uint32_t vi = 0;
    for ( uint32_t pi = 0; pi < pc; ++pi ) {
        assert( 3 == mesh->GetPolygonSize( pi ) );

        // Having this array we can easily control polygon winding order.
        // Since mesh is triangular we can make it static [3] at compile-time.
        for ( const uint32_t pvi : {0, 1, 2} ) {
            const uint32_t ci = (uint32_t) mesh->GetPolygonVertex( (int) pi, (int) pvi );

            const auto cp = mesh->GetControlPointAt( ci );
            const auto uv = GetElementValue< FbxGeometryElementUV, FbxVector2 >( uve, ci, vi, pi );
            const auto n  = GetElementValue< FbxGeometryElementNormal, FbxVector4 >( ne, ci, vi, pi );
            const auto t  = GetElementValue< FbxGeometryElementTangent, FbxVector4 >( te, ci, vi, pi );

            auto& vvii          = vertices[ vi ];
            vvii.position[ 0 ]  = (float) cp[ 0 ];
            vvii.position[ 1 ]  = (float) cp[ 1 ];
            vvii.position[ 2 ]  = (float) cp[ 2 ];
            vvii.normal[ 0 ]    = (float) n[ 0 ];
            vvii.normal[ 1 ]    = (float) n[ 1 ];
            vvii.normal[ 2 ]    = (float) n[ 2 ];
            vvii.tangent[ 0 ]   = (float) t[ 0 ];
            vvii.tangent[ 1 ]   = (float) t[ 1 ];
            vvii.tangent[ 2 ]   = (float) t[ 2 ];
            vvii.tangent[ 3 ]   = (float) t[ 3 ];
            vvii.texCoords[ 0 ] = (float) uv[ 0 ];
            vvii.texCoords[ 1 ] = (float) uv[ 1 ];

            assert( !isnan( (float) cp[ 0 ] ) && !isnan( (float) cp[ 1 ] ) && !isnan( (float) cp[ 2 ] ) );
            assert( !isnan( (float) n[ 0 ] ) && !isnan( (float) n[ 1 ] ) && !isnan( (float) n[ 2 ] ) );
            assert( !isnan( (float) t[ 0 ] ) && !isnan( (float) t[ 1 ] ) && !isnan( (float) t[ 2 ] ) && !isnan( (float) t[ 3 ] ) );
            assert( !isnan( (float) uv[ 0 ] ) && !isnan( (float) uv[ 1 ] ) );

            positionMin.x = std::min( positionMin.x, (float) cp[ 0 ] );
            positionMin.y = std::min( positionMin.y, (float) cp[ 1 ] );
            positionMin.z = std::min( positionMin.z, (float) cp[ 2 ] );
            positionMax.x = std::max( positionMax.x, (float) cp[ 0 ] );
            positionMax.y = std::max( positionMax.y, (float) cp[ 1 ] );
            positionMax.z = std::max( positionMax.z, (float) cp[ 2 ] );

            texcoordMin.x = std::min( texcoordMin.x, (float) uv[ 0 ] );
            texcoordMin.y = std::min( texcoordMin.y, (float) uv[ 1 ] );
            texcoordMax.x = std::max( texcoordMax.x, (float) uv[ 0 ] );
            texcoordMax.y = std::max( texcoordMax.y, (float) uv[ 1 ] );

            ++vi;
        }
    }

    m.positionMin = apemodefb::vec3( positionMin.x, positionMin.y, positionMin.z );
    m.positionMax = apemodefb::vec3( positionMax.x, positionMax.y, positionMax.z );
    m.texcoordMin = apemodefb::vec2( texcoordMin.x, texcoordMin.y );
    m.texcoordMax = apemodefb::vec2( texcoordMax.x, texcoordMax.y );

    if ( nullptr == uve ) {
        s.console->error( "Mesh \"{}\" does not have texcoords geometry layer.",
                          mesh->GetNode( )->GetName( ) );
    }

    if ( nullptr == ne ) {
        s.console->warn( "Mesh \"{}\" does not have normal geometry layer.",
                          mesh->GetNode( )->GetName( ) );

        // Calculate face normals ourselves.
        // Usable but incorrect.
        CalculateFaceNormals( vertices, vertexCount );
    }

    if ( nullptr == te && nullptr != uve ) {
        s.console->warn( "Mesh \"{}\" does not have tangent geometry layer.",
                         mesh->GetNode( )->GetName( ) );

        // Calculate tangents ourselves if UVs are available.
        CalculateTangents( vertices, vertexCount );
    }
}

//
// See implementation in fbxpmeshopt.cpp.
//

void Optimize32( apemode::Mesh& mesh, apemodefb::StaticVertexFb const* vertices, uint32_t & vertexCount, uint32_t vertexStride );
void Optimize16( apemode::Mesh& mesh, apemodefb::StaticVertexFb const* vertices, uint32_t & vertexCount, uint32_t vertexStride );

//
// See implementation in fbxpmeshpacking.cpp.
//

void Pack( const apemodefb::StaticVertexFb* vertices,
           apemodefb::PackedVertexFb*       packed,
           const uint32_t                  vertexCount,
           const mathfu::vec3              positionMin,
           const mathfu::vec3              positionMax,
           const mathfu::vec2              texcoordsMin,
           const mathfu::vec2              texcoordsMax );

template < typename TIndex >
void ExportMesh( FbxNode* node, FbxMesh* mesh, apemode::Node& n, apemode::Mesh& m, uint32_t vertexCount, bool pack, bool optimize ) {
    auto& s = apemode::Get( );

    const uint16_t vertexStride           = (uint16_t) sizeof( apemodefb::StaticVertexFb );
    const uint32_t vertexBufferSize       = vertexCount * vertexStride;
    const uint16_t packedVertexStride     = (uint16_t) sizeof( apemodefb::PackedVertexFb );
    const uint32_t packedVertexBufferSize = vertexCount * packedVertexStride;

    m.vertices.resize( vertexBufferSize );

    std::vector< TIndex > tempSubsetIndices;
    std::vector< std::tuple< TIndex, TIndex > > tempItems;

    mathfu::vec3 positionMin;
    mathfu::vec3 positionMax;
    mathfu::vec2 texcoordMin;
    mathfu::vec2 texcoordMax;

    InitializeVertices( mesh,
                        m,
                        reinterpret_cast< StaticVertex* >( m.vertices.data( ) ),
                        vertexCount,
                        positionMin,
                        positionMax,
                        texcoordMin,
                        texcoordMax );

    GetSubsets( mesh, m, tempSubsetIndices, m.subsets, m.subsetsPolies, tempItems );

    if ( const size_t subsetIndexBufferSize = sizeof( TIndex ) * tempSubsetIndices.size( ) ) {
        m.subsetIndices.resize( subsetIndexBufferSize );
        std::memcpy( m.subsetIndices.data( ), tempSubsetIndices.data( ), subsetIndexBufferSize );
    }

    if ( std::is_same< TIndex, uint16_t >::value ) {
        m.subsetIndexType = apemodefb::EIndexTypeFb_UInt16;
    } else if ( std::is_same< TIndex, uint32_t >::value ) {
        m.subsetIndexType = apemodefb::EIndexTypeFb_UInt32;
    } else {
        assert( false );
    }

    if ( optimize ) {
        auto initializedVertices = reinterpret_cast< const apemodefb::StaticVertexFb* >( m.vertices.data( ) );

        if ( std::is_same< TIndex, uint16_t >::value ) {
            Optimize16( m, initializedVertices, vertexCount, vertexStride );
        } else if ( std::is_same< TIndex, uint32_t >::value ) {
            m.subsetIndexType = apemodefb::EIndexTypeFb_UInt32;
            Optimize32( m, initializedVertices, vertexCount, vertexStride );
        }
    }

    if ( pack ) {
        std::vector< apemodefb::StaticVertexFb > tempBuffer;
        tempBuffer.resize( vertexCount );
        memcpy( tempBuffer.data( ), m.vertices.data( ), m.vertices.size( ) );

        m.vertices.resize( packedVertexBufferSize );
        Pack( reinterpret_cast< apemodefb::StaticVertexFb* >( tempBuffer.data( ) ),
              reinterpret_cast< apemodefb::PackedVertexFb* >( m.vertices.data( ) ),
              vertexCount,
              positionMin,
              positionMax,
              texcoordMin,
              texcoordMax );
    }

    apemodefb::vec3 bboxMin( positionMin.x, positionMin.y, positionMin.z );
    apemodefb::vec3 bboxMax( positionMax.x, positionMax.y, positionMax.z );
    apemodefb::vec2 uvMin( texcoordMin.x, texcoordMin.y );
    apemodefb::vec2 uvMax( texcoordMax.x, texcoordMax.y );

    if ( pack ) {
        auto const positionScale = positionMax - positionMin;
        auto const texcoordScale = texcoordMax - texcoordMin;
        apemodefb::vec3 bboxScale( positionScale.x, positionScale.y, positionScale.z );
        apemodefb::vec2 uvScale( texcoordScale.x, texcoordScale.y );

        m.submeshes.emplace_back( bboxMin,                        // bbox min
                                  bboxMax,                        // bbox max
                                  bboxMin,                        // position offset
                                  bboxScale,                      // position scale
                                  uvMin,                          // uv offset
                                  uvScale,                        // uv scale
                                  0,                              // base vertex
                                  vertexCount,                    // vertex count
                                  0,                              // base index
                                  0,                              // index count
                                  0,                              // base subset
                                  (uint32_t) m.subsets.size( ),   // subset count
                                  apemodefb::EVertexFormat_Packed, // vertex format
                                  packedVertexStride              // vertex stride
                                  );
    } else {
        m.submeshes.emplace_back( bboxMin,                            // bbox min
                                  bboxMax,                            // bbox max
                                  apemodefb::vec3( 0.0f, 0.0f, 0.0f ), // position offset
                                  apemodefb::vec3( 1.0f, 1.0f, 1.0f ), // position scale
                                  apemodefb::vec2( 0.0f, 0.0f ),       // uv offset
                                  apemodefb::vec2( 1.0f, 1.0f ),       // uv scale
                                  0,                                  // base vertex
                                  vertexCount,                        // vertex count
                                  0,                                  // base index
                                  0,                                  // index count
                                  0,                                  // base subset
                                  (uint32_t) m.subsets.size( ),       // subset count
                                  apemodefb::EVertexFormat_Static,     // vertex format
                                  vertexStride                        // vertex stride
                                  );
    }
}

void ExportMesh( FbxNode* node, apemode::Node& n, bool pack, bool optimize ) {
    auto& s = apemode::Get( );
    if ( auto mesh = node->GetMesh( ) ) {
        s.console->info( "Node \"{}\" has mesh.", node->GetName( ) );
        if ( !mesh->IsTriangleMesh( ) ) {
            s.console->warn( "Mesh \"{}\" is not triangular, processing...", node->GetName( ) );
            FbxGeometryConverter converter( mesh->GetNode( )->GetFbxManager( ) );
            mesh = (FbxMesh*) converter.Triangulate( mesh, true, s.legacyTriangulationSdk );

            if ( nullptr == mesh ) {
                s.console->error( "Mesh \"{}\" triangulation failed (mesh will be skipped).", node->GetName( ) );
                return;
            }

            s.console->warn( "Mesh \"{}\" was triangulated (success).", node->GetName( ) );
        }

        if ( const auto deformerCount = mesh->GetDeformerCount( ) ) {
            s.console->warn( "Mesh \"{}\" has {} deformers (ignored).", node->GetName( ), deformerCount );
        }

        n.meshId = (uint32_t) s.meshes.size( );
        s.meshes.emplace_back( );
        apemode::Mesh& m = s.meshes.back( );

        const uint32_t vertexCount = mesh->GetPolygonCount( ) * 3;

        if ( vertexCount < 0xffff )
            ExportMesh< uint16_t >( node, mesh, n, m, vertexCount, pack, optimize );
        else
            ExportMesh< uint32_t >( node, mesh, n, m, vertexCount, pack, optimize );
    }
}
