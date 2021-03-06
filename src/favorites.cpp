﻿/*
 *
 *  Copyright ( c ) 2011-2015
 *  name : Francis Banyikwa
 *  email: mhogomchungu@gmail.com
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  ( at your option ) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "favorites.h"

#include "utility.h"
#include "settings.h"
#include "crypto.h"

#include <QDir>
#include <QFile>
#include <QCryptographicHash>

static utility::qstring_result _config_path()
{
	QString m = settings::instance().ConfigLocation() + "/favorites/" ;

	if( utility::pathExists( m ) ){

		return m ;
	}else{
		if( QDir().mkpath( m ) ){

			return m ;
		}else{
			return {} ;
		}
	}
}

static QString _create_path( const QString& m,const favorites::entry& e )
{
	auto a = utility::split( e.volumePath,'@' ).last() ;

	a = utility::split( a,'/' ).last() ;

	a.replace( ":","" ) ;

	auto b = a + e.mountPointPath ;

	return m + a + "-" + crypto::sha256( b ) + ".json" ;
}

static QString _create_path( const favorites::entry& e )
{
	auto s = _config_path() ;

	if( s.has_value() ){

		return _create_path( s.value(),e ) ;
	}else{
		return {} ;
	}
}

static void _move_favorites_to_new_system( const QStringList& m )
{
	favorites::entry s ;

	QString autoMountVolume ;

	utility2::stringListToStrings( m,
				       s.volumePath,
				       s.mountPointPath,
				       autoMountVolume,
				       s.configFilePath,
				       s.idleTimeOut,
				       s.mountOptions ) ;

	if( autoMountVolume != "N/A" ){

		if( autoMountVolume == "true" ){

			s.autoMount = true ;
		}else{
			s.autoMount = false ;
		}
	}

	if( s.configFilePath == "N/A" ){

		s.configFilePath.clear() ;
	}

	if( s.idleTimeOut == "N/A" ){

		s.idleTimeOut.clear() ;
	}

	if( s.mountOptions == "N/A" ){

		s.mountOptions.clear() ;
	}

	s.reverseMode          = s.mountOptions.contains( "-SiriKaliReverseMode" ) ;
	s.volumeNeedNoPassword = s.mountOptions.contains( "-SiriKaliVolumeNeedNoPassword" ) ;

	if( s.mountOptions.contains( "-SiriKaliMountReadOnly" ) ){

		s.readOnlyMode = true ;
	}

	s.mountOptions.replace( "-SiriKaliMountReadOnly","" ) ;
	s.mountOptions.replace( "-SiriKaliVolumeNeedNoPassword","" ) ;
	s.mountOptions.replace( "-SiriKaliReverseMode","" ) ;

	favorites::instance().add( s ) ;
}

static void _log_error( const QString& msg,const QString& path )
{
	auto a = "\nFailed to parse file for reading: " + path ;

	utility::debug::showDebugWindow( msg + a ) ;
}

utility2::result< favorites::entry > favorites::readFavoriteByPath( const QString& path ) const
{
	try {
		SirikaliJson json( path,
				   SirikaliJson::type::PATH,
				   []( const QString& e ){ utility::debug() << e ; } ) ;

		favorites::entry m ;

		m.reverseMode          = json.getBool( "reverseMode" ) ;
		m.volumeNeedNoPassword = json.getBool( "volumeNeedNoPassword" ) ;
		m.volumePath           = json.getString( "volumePath" ) ;
		m.mountPointPath       = json.getString( "mountPointPath" ) ;
		m.configFilePath       = json.getString( "configFilePath" ) ;
		m.idleTimeOut          = json.getString( "idleTimeOut" ) ;
		m.mountOptions         = json.getString( "mountOptions" ) ;
		m.preMountCommand      = json.getString( "preMountCommand" ) ;
		m.postMountCommand     = json.getString( "postMountCommand" ) ;
		m.preUnmountCommand    = json.getString( "preUnmountCommand" ) ;
		m.postUnmountCommand   = json.getString( "postUnmountCommand" ) ;

		m.keyFile              = json.getString( "keyFilePath" ) ;

		favorites::triState::readTriState( json,m.readOnlyMode,"mountReadOnly" ) ;
		favorites::triState::readTriState( json,m.autoMount,"autoMountVolume" ) ;

		return m ;

	}catch( const std::exception& e ){

		_log_error( e.what(),path ) ;

	}catch( ... ){

		_log_error( "Unknown error has occured",path ) ;
	}

	return {} ;
}

std::vector<favorites::entry> favorites::readFavorites() const
{
	const auto m = _config_path() ;

	if( !m.has_value() ){

		return {} ;
	}

	const auto& a = m.value() ;

	const auto s = QDir( a ).entryList( QDir::Filter::Files | QDir::Filter::Hidden ) ;

	std::vector< favorites::entry > e ;

	for( const auto& it : s ){

		auto mm = this->readFavoriteByPath( a + it ) ;

		if( mm ){

			e.emplace_back( mm.RValue() ) ;
		}
	}

	return e ;
}

utility2::result< favorites::entry > favorites::readFavorite( const QString& e,const QString& s ) const
{
	if( s.isEmpty() ){

		for( const auto& it : favorites::readFavorites() ){

			if( it.volumePath == e ){

				return it ;
			}
		}
	}else{
		for( const auto& it : favorites::readFavorites() ){

			if( it.volumePath == e && it.mountPointPath == s ){

				return it ;
			}
		}
	}

	return {} ;
}

void favorites::updateFavorites()
{
	auto& m = settings::instance().backend() ;

	if( m.contains( "FavoritesVolumes" ) ){

		const auto a = m.value( "FavoritesVolumes" ).toStringList() ;

		m.remove( "FavoritesVolumes" ) ;

		for( const auto& it : a ){

			_move_favorites_to_new_system( utility::split( it,'\t' ) ) ;
		}
	}
}

favorites::error favorites::add( const favorites::entry& e )
{
	auto m = _config_path() ;

	if( !m.has_value() ){

		return error::FAILED_TO_CREATE_ENTRY ;
	}

	auto a = _create_path( m.value(),e ) ;

	try{
		SirikaliJson json( []( const QString& e ){ utility::debug() << e ; } ) ;

		json[ "volumePath" ]           = e.volumePath ;
		json[ "mountPointPath" ]       = e.mountPointPath ;
		json[ "configFilePath" ]       = e.configFilePath ;
		json[ "keyFilePath" ]          = e.keyFile ;
		json[ "idleTimeOut" ]          = e.idleTimeOut ;
		json[ "mountOptions" ]         = e.mountOptions ;
		json[ "preMountCommand" ]      = e.preMountCommand ;
		json[ "postMountCommand" ]     = e.postMountCommand ;
		json[ "preUnmountCommand" ]    = e.preUnmountCommand ;
		json[ "postUnmountCommand" ]   = e.postUnmountCommand ;
		json[ "reverseMode" ]          = e.reverseMode ;
		json[ "volumeNeedNoPassword" ] = e.volumeNeedNoPassword ;

		favorites::triState::writeTriState( json,e.readOnlyMode,"mountReadOnly" ) ;
		favorites::triState::writeTriState( json,e.autoMount,"autoMountVolume" ) ;

		if( utility::pathExists( a ) ){

			return error::ENTRY_ALREADY_EXISTS ;
		}else{
			if( json.toFile( a ) ){

				return error::SUCCESS ;
			}else{
				return error::FAILED_TO_CREATE_ENTRY ;
			}
		}

	}catch( const std::exception& e ){

		_log_error( e.what(),a ) ;

	}catch( ... ){

		_log_error( "Unknown error has occured",a ) ;
	}

	return error::FAILED_TO_CREATE_ENTRY ;
}

void favorites::replaceFavorite( const favorites::entry& old,const favorites::entry& New )
{
	favorites::removeFavoriteEntry( old ) ;
	favorites::add( New ) ;
}

void favorites::removeFavoriteEntry( const favorites::entry& e )
{
	auto s = _create_path( e ) ;

	if( !s.isEmpty() ){

		QFile( s ).remove() ;
	}
}

favorites::entry::entry()
{
}

favorites::entry::entry( const QString& e )
{
	volumePath = e ;
}
