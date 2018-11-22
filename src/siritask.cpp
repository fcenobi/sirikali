/*
 *
 *  Copyright (c) 2014-2015
 *  name : Francis Banyikwa
 *  email: mhogomchungu@gmail.com
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "plugins.h"
#include "siritask.h"
#include "mountinfo.h"
#include "winfsp.h"
#include "settings.h"

#include <QDir>
#include <QString>
#include <QDebug>
#include <QFile>

static bool _create_folder( const QString& m )
{
	if( utility::platformIsWindows() ){

		return true ;
	}else{
		if( utility::pathExists( m ) ){

			return settings::instance().reUseMountPoint() ;
		}else{
			return utility::createFolder( m ) ;
		}
	}
}

static QString _makePath( const QString& e )
{
	return utility::Task::makePath( e ) ;
}

template< typename T >
static bool _ecryptfs( const T& e )
{
	return utility::equalsAtleastOne( e,"ecryptfs","ecryptfs-simple" ) ;
}

static bool _ecryptfs_illegal_path( const engines::engine::options& opts )
{
	if( _ecryptfs( opts.type ) && utility::useSiriPolkit() ){

		return opts.cipherFolder.contains( " " ) || opts.plainFolder.contains( " " ) ;
	}else{
		return false ;
	}
}

template< typename ... T >
static bool _deleteFolders( const T& ... m )
{
	bool s = false ;

	QDir e ;

	for( const auto& it : { m ... } ){

		s = e.rmdir( it ) ;
	}

	return s ;
}

static void _run_command_on_mount( const engines::engine::options& opt,const QString& app )
{
	auto exe = settings::instance().runCommandOnMount() ;

	if( !exe.isEmpty() ){

		auto a = _makePath( opt.cipherFolder ) ;
		auto b = _makePath( opt.plainFolder ) ;

		exe = QString( "%1 %2 %3 %4" ).arg( exe,a,b,app ) ;

		QProcess e ;

		e.setProcessEnvironment( utility::systemEnvironment() ) ;

		e.startDetached( exe ) ;
	}
}

bool siritask::deleteMountFolder( const QString& m )
{
	if( settings::instance().reUseMountPoint() ){

		return false ;
	}else{
		if( utility::platformIsWindows() ){

			return true ;
		}else{
			return _deleteFolders( m ) ;
		}
	}
}

static utility::result< utility::Task > _unmount_volume( const QString& exe,
							 const QString& mountPoint,
							 bool usePolkit )
{
	auto e = settings::instance().preUnMountCommand() ;

	int timeOut = 10000 ;

	if( e.isEmpty() ){

		return utility::Task::run( exe,timeOut,usePolkit ).get() ;
	}else{
		if( utility::Task::run( e + " " + mountPoint,timeOut,false ).get().success() ){

			return utility::Task::run( exe,timeOut,usePolkit ).get() ;
		}else{
			return {} ;
		}
	}
}

template< typename Function >
static bool _unmount_ecryptfs_( Function cmd,const QString& mountPoint,bool& not_set )
{
	auto s = _unmount_volume( cmd(),mountPoint,true ) ;

	if( s && s.value().success() ){

		return true ;
	}else{
		if( not_set && s.value().stdError().contains( "error: failed to set gid" ) ){

			not_set = false ;

			if( utility::enablePolkit( utility::background_thread::True ) ){

				auto s = _unmount_volume( cmd(),mountPoint,true ) ;
				return s && s.value().success() ;
			}
		}

		return false ;
	}
}

static bool _unmount_ecryptfs( const QString& cipherFolder,const QString& mountPoint,int maxCount )
{
	bool not_set = true ;

	auto cmd = [ & ](){

		auto exe = utility::executableFullPath( "ecryptfs-simple" ) ;

		auto s = exe + " -k " + cipherFolder ;

		if( utility::useSiriPolkit() ){

			return utility::wrap_su( s ) ;
		}else{
			return s ;
		}
	} ;

	if( _unmount_ecryptfs_( cmd,mountPoint,not_set ) ){

		return true ;
	}else{
		for( int i = 1 ; i < maxCount ; i++ ){

			utility::Task::waitForOneSecond() ;

			if( _unmount_ecryptfs_( cmd,mountPoint,not_set ) ){

				return true ;
			}
		}

		return false ;
	}
}

static bool _unmount_rest_( const QString& cmd,const QString& mountPoint )
{
	auto s = _unmount_volume( cmd,mountPoint,false ) ;

	return s && s.value().success() ;
}

static bool _unmount_rest( const QString& mountPoint,int maxCount )
{
	if( utility::platformIsWindows() ){

		return SiriKali::Winfsp::FspLaunchStop( mountPoint ).success() ;
	}

	auto cmd = [ & ](){

		if( utility::platformIsOSX() ){

			return "umount " + mountPoint ;
		}else{
			return "fusermount -u " + mountPoint ;
		}
	}() ;

	if( _unmount_rest_( cmd,mountPoint ) ){

		return true ;
	}else{
		for( int i = 1 ; i < maxCount ; i++ ){

			utility::Task::waitForOneSecond() ;

			if( _unmount_rest_( cmd,mountPoint ) ){

				return true ;
			}
		}

		return false ;
	}
}

Task::future< bool >& siritask::encryptedFolderUnMount( const QString& cipherFolder,
							const QString& mountPoint,
							const QString& fileSystem,
							int numberOfAttempts )
{
	return Task::run( [ = ](){

		if( _ecryptfs( fileSystem ) ){

			auto a = _makePath( cipherFolder ) ;
			auto b = _makePath( mountPoint ) ;

			return _unmount_ecryptfs( a,b,numberOfAttempts ) ;
		}else{
			return _unmount_rest( _makePath( mountPoint ),numberOfAttempts ) ;
		}
	} ) ;
}

static utility::Task _run_task( const QString& cmd,
				const QString& password,
				const engines::engine::options& opts,
				bool create,
				bool ecryptfs )
{
	if( utility::platformIsWindows() ){

		if( create ){

			return SiriKali::Winfsp::FspLaunchRun( cmd,password.toLatin1(),opts ) ;
		}else{
			return SiriKali::Winfsp::FspLaunchStart( cmd,password.toLatin1(),opts ) ;
		}
	}else{
		return utility::Task( cmd,20000,utility::systemEnvironment(),
				      password.toLatin1(),[](){},ecryptfs ) ;
	}
}

static utility::result< QString > _build_config_file_path( const engines::engine& engine,
							   const QString& configFilePath )
{
	if( configFilePath.isEmpty() ){

		return QString() ;
	}else{
		auto s = engine.setConfigFilePath( _makePath( configFilePath ) ) ;

		if( s.isEmpty() ){

			return {} ;
		}else{
			return s ;
		}
	}
}

static engines::engine::cmdStatus _cmd( const engines::engine& engine,
					bool create,
					const engines::engine::options& opts,
					const QString& password,
					const QString& configFilePath )
{
	auto exe = engine.executableFullPath() ;

	if( exe.isEmpty() ){

		return engine.notFoundCode() ;
	}else{
		exe = utility::Task::makePath( exe ) ;

		auto _run = [ & ]()->engines::engine::cmdStatus{

			auto m = _build_config_file_path( engine,configFilePath ) ;

			if( m ){

				auto cc = _makePath( opts.cipherFolder ) ;
				auto mm = _makePath( opts.plainFolder ) ;

				auto cmd = engine.command( { exe,opts,m.value(),cc,mm,create } ) ;

				auto s = _run_task( cmd,password,opts,create,_ecryptfs( engine.name() ) ) ;

				if( s.success() ){

					return { engines::engine::status::success,s.exitCode() } ;
				}else{
					auto m = s.stdError().isEmpty() ? s.stdOut() : s.stdError() ;

					auto n = engine.errorCode( m.toLower(),s.exitCode() ) ;

					return { n,s.exitCode(),m } ;
				}
			}else{
				return engines::engine::status::backEndDoesNotSupportCustomConfigPath ;
			}
		} ;

		auto s = _run() ;

		if( s == engines::engine::status::ecrypfsBadExePermissions ){

			if( utility::enablePolkit( utility::background_thread::True ) ){

				s = _run() ;
			}
		}

		return s ;
	}
}

static QString _configFilePath( const engines::engine::options& opt )
{
	if( opt.configFilePath.isEmpty() ){

		return QString() ;
	}else{
		return QDir().absoluteFilePath( opt.configFilePath ) ;
	}
}

static engines::engine::cmdStatus _mount( bool reUseMountPoint,
				   const engines::engine& engine,
				   const engines::engine::options& copt,
				   const QString& configFilePath )
{
	auto opt = copt ;

	opt.type = engine.name() ;

	if( _ecryptfs_illegal_path( opt ) ){

		return engines::engine::status::ecryptfsIllegalPath ;
	}

	if( _create_folder( opt.plainFolder ) || reUseMountPoint ){

		auto e = _cmd( engine,false,opt,opt.key,configFilePath ) ;

		if( e == engines::engine::status::success ){

			_run_command_on_mount( opt,opt.type ) ;
		}else{
			siritask::deleteMountFolder( opt.plainFolder ) ;
		}

		return e ;
	}else{
		return engines::engine::status::failedToCreateMountPoint ;
	}
}

static utility::result< QString > _path_exist( QString e,const QString& m )
{
	e.remove( 0,m.size() ) ;

	if( utility::pathExists( e ) ){

		return e ;
	}else{
		return {} ;
	}
}

static engines::engine::cmdStatus _encrypted_folder_mount( const engines::engine::options& opt,bool reUseMP )
{
	const auto& engines = engines::instance() ;

	if( opt.cipherFolder.startsWith( "sshfs " ) ){

		const auto& engine = engines.getByName( "sshfs" ) ;

		auto opts = opt ;
		opts.cipherFolder = opts.cipherFolder.remove( 0,6 ) ; // 6 is the size of "sshfs "

		if( !opts.key.isEmpty() ){

			opts.key = engine.setPassword( opts.key ) ;
		}

		return _mount( reUseMP,engine,opts,QString() ) ;

	}else if( opt.configFilePath.isEmpty() ){

		auto m = engines.getByConfigFileNames( [ & ]( const QString& e ){

			return utility::pathExists( opt.cipherFolder + "/" + e ) ;
		} ) ;

		const auto& engine = m.first ;

		if( engine.known() ){

			if( m.second.endsWith( "gocryptfs.reverse.conf" ) ){

				if( opt.reverseMode ){

					return _mount( reUseMP,engine,opt,QString() ) ;
				}else{
					auto opts = opt ;
					opts.reverseMode = true ;
					return _mount( reUseMP,engine,opts,QString() ) ;
				}

			}else if( engine.name() == "ecryptfs" ){

				return _mount( reUseMP,engine,opt,opt.cipherFolder + "/" + m.second ) ;
			}else{
				return _mount( reUseMP,engine,opt,QString() ) ;
			}
		}

	}else if( utility::pathExists( opt.configFilePath ) ){

		auto m = engines.getByConfigFileNames( [ & ]( const QString& e ){

			return opt.configFilePath.endsWith( e ) ;
		} ) ;

		const auto& engine = m.first ;

		if( engine.known() ){

			return _mount( reUseMP,engine,opt,opt.configFilePath ) ;
		}
	}else{
		auto e = opt.configFilePath ;

		for( const auto& it : engines::supported() ){

			auto n = it.toLower() ;

			auto s = "[[[" + n + "]]]" ;

			if( e.startsWith( s ) ){

				auto m = _path_exist( e,s ) ;

				if( m ){

					return _mount( reUseMP,engines.getByName( n ),opt,m.value() ) ;
				}
			}
		}
	}

	return engines::engine::status::unknown ;
}

static engines::engine::cmdStatus _encrypted_folder_create( const engines::engine::options& opt )
{
	if( _ecryptfs_illegal_path( opt ) ){

		return engines::engine::status::ecryptfsIllegalPath ;
	}

	if( _create_folder( opt.cipherFolder ) ){

		if( _create_folder( opt.plainFolder ) ){

			auto& m = engines::instance().getByName( opt ) ;

			auto e = _cmd( m,true,opt,m.setPassword( opt.key ),[ & ](){

				auto e = _configFilePath( opt ) ;

				if( e.isEmpty() && m.name() == "ecryptfs" ){

					return opt.cipherFolder + "/" + m.configFileName() ;
				}else{
					return e ;
				}
			}() ) ;

			if( e == engines::engine::status::success ){

				if( !m.autoMountsOnCreate() ){

					auto e = siritask::encryptedFolderMount( opt,true ).get() ;

					if( e != engines::engine::status::success ){

						_deleteFolders( opt.cipherFolder,opt.plainFolder ) ;
					}
				}
			}else{
				_deleteFolders( opt.plainFolder,opt.cipherFolder ) ;
			}

			return e ;
		}else{
			_deleteFolders( opt.cipherFolder ) ;

			return engines::engine::status::failedToCreateMountPoint ;
		}
	}else{
		return engines::engine::status::failedToCreateMountPoint ;
	}
}

Task::future< engines::engine::cmdStatus >& siritask::encryptedFolderCreate( const engines::engine::options& opt )
{
	return Task::run( _encrypted_folder_create,opt ) ;
}

Task::future< engines::engine::cmdStatus >& siritask::encryptedFolderMount( const engines::engine::options& opt,
									    bool reUseMountPoint )
{
	return Task::run( _encrypted_folder_mount,opt,reUseMountPoint ) ;
}
