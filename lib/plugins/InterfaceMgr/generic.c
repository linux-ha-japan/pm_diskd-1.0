/*
 * 
 * Generic interface (implementation) manager
 *
 * Copyright 2001 Alan Robertson <alanr@unix.sh>
 * Licensed under the GNU Lesser General Public License
 *
 * This manager will manage any number of types of interfaces.
 *
 * This means that when any implementations of our client interfaces register
 * or unregister, it is us that makes their interfaces show up in the outside
 * world.
 *
 * And, of course, we have to do this in a very generic way, since we have
 * no idea about the client programs or interface types, or anything else.
 *
 * We do that by getting a parameter passed to us which tell us the names
 * of the interface types we want to manage, and the address of a GHashTable
 * for each type that we put the implementation in when they register
 * themselves.
 *
 * So, each type of interface that we manage gets its own private
 * GHashTable of the implementations of that type that are currently
 * registered.
 *
 * For example, if we manage communication modules, their exported
 * interfaces will be registered in a hash table.  If we manage
 * authentication modules, they'll have their (separate) hash table that
 * their exported interfaces are registered in.
 *
 */

#define	PIL_PLUGINTYPE		InterfaceMgr
#define PIL_PLUGINTYPE_S	"InterfaceMgr"
#define	PIL_PLUGIN		generic
#define PIL_PLUGIN_S		"generic"

/* We are an interface manager... */
#define ENABLE_PLUGIN_MANAGER_PRIVATE

#include <pils/generic.h>

#include <stdio.h>

PIL_PLUGIN_BOILERPLATE("1.0", GenDebugFlag, CloseGeneralPluginManager)

/*
 * Key is interface type, value is a PILGenericIfMgmtRqst.
 * The key is g_strdup()ed, but the struct is not copied.
 */
static GHashTable*	MasterTable = NULL;

static gboolean FreeAKey(gpointer key, gpointer value, gpointer data);

/*
 *	Places to store information gotten during registration.
 */
static const PILPluginImports*	GenPIImports;	/* Imported plugin fcns */
static PILPlugin*		GenPlugin;	/* Our plugin info */
static PILInterfaceImports*	GenIfImports;	/* Interface imported fcns */

/* Our exported generic interface management functions */
static PIL_rc RegisterGenIF(PILInterface* ifenv, void**	imports);

static PIL_rc UnregisterGenIF(PILInterface*iifinfo);

static PIL_rc CloseGenInterfaceManager(PILInterface*, void* info);

/*
 *	Our Interface Manager interfaces - exported to the universe!
 *
 *	(or at least to the interface management universe ;-).
 *
 *	These are the interfaces which are used to manage our
 *	client implementations
 */
static PILInterfaceOps		GenIfOps =
{	RegisterGenIF
,	UnregisterGenIF
};


PIL_rc PIL_PLUGIN_INIT(PILPlugin*us, PILPluginImports* imports, void*);

/*
 *	Our user_ptr is presumed to point to NULL-terminated array of
 *	PILGenericIfMgmtRqst structs.
 *
 *	These requests have pointers to GHashTables for us
 *	to put plugins into when they show up, and drop from when
 *	they disappear.
 *
 * 	Issues include:
 * 	- freeing all memory,
 * 	- making sure things are all cleaned up correctly
 * 	- Thread-safety?
 *
 * 	IMHO the global system should handle thread-safety.
 */
static PIL_rc AddAnInterfaceType(PILPlugin*us, PILGenericIfMgmtRqst* req);

PIL_rc
PIL_PLUGIN_INIT(PILPlugin*us, PILPluginImports* imports, void *user_ptr)
{
	PIL_rc			ret;
	PILGenericIfMgmtRqst*	user_req;
	PILGenericIfMgmtRqst*	curreq;
	/*
	 * Force the compiler to check our parameters...
	 */
	PILPluginInitFun	fun = &PIL_PLUGIN_INIT; (void)fun;


	if (GenDebugFlag) {
		GenPIImports->log(PIL_DEBUG
		,	"IF manager %s: initializing.", PIL_PLUGIN_S);
	}

	if (user_ptr == NULL) {
		imports->log(PIL_CRIT
		,	"%s Interface Manager requires non-NULL "
		" PILGenericIfMgmtRqst user pointer at initialization."
		,	PIL_PLUGIN_S);
		return PIL_INVAL;
	}

	GenPIImports = imports;
	GenPlugin = us;

	if (GenDebugFlag) {
		GenPIImports->log(PIL_DEBUG
		,	"IF manager %s: registering as a plugin."
		, PIL_PLUGIN_S);
	}

	/* Register ourselves as a plugin */

	if ((ret = imports->register_plugin(us, &OurPIExports)) != PIL_OK) {
		imports->log(PIL_CRIT
		,	"IF manager %s unable to register as plugin (%s)"
		,	PIL_PLUGIN_S, PIL_strerror(ret));

		return ret;
	}

	user_req = user_ptr;
	MasterTable = g_hash_table_new(g_str_hash, g_str_equal);

	/*
	 * Register to manage implementations
	 * for all the interface types we've been asked to manage.
	 */

	for(curreq = user_req; curreq->iftype != NULL; ++curreq) {
		PIL_rc newret;

		newret = AddAnInterfaceType(us, curreq);

		if (newret != PIL_OK) {
			ret = newret;
		}
	}

	return ret;
}

static PIL_rc
AddAnInterfaceType(PILPlugin*us, PILGenericIfMgmtRqst* req)
{
	PIL_rc	rc;
	PILInterface*		GenIf;		/* Our Generic Interface info*/

	g_hash_table_insert(MasterTable, g_strdup(req->iftype), req);

	if (req->ifmap == NULL) {
		GenPIImports->log(PIL_CRIT
		,	"IF manager %s: iftype %s has NULL"
		" ifmap pointer address."
		,	PIL_PLUGIN_S, req->iftype);
		return PIL_INVAL;
	}
	if ((*req->ifmap) != NULL) {
		GenPIImports->log(PIL_CRIT
		,	"IF manager %s: iftype %s GHashTable pointer"
		" was not initialized to NULL"
		,	PIL_PLUGIN_S, req->iftype);
		return PIL_INVAL;
	}

	if (GenDebugFlag) {
		GenPIImports->log(PIL_DEBUG
		,	"IF manager %s: registering ourselves"
		" to manage interface type %s"
		,	PIL_PLUGIN_S, req->iftype);
		GenPIImports->log(PIL_DEBUG
		,	"%s IF manager: ifmap: 0x%lx callback: 0x%lx"
		" imports: 0x%lx"
		,	PIL_PLUGIN_S
		,	(unsigned long)req->ifmap
		,	(unsigned long)req->callback
		,	(unsigned long)req->importfuns);
	}

	/* Create the hash table to communicate with this client */
	*(req->ifmap) = g_hash_table_new(g_str_hash, g_str_equal);

	rc = GenPIImports->register_interface(us
	,	PIL_PLUGINTYPE_S
	,	req->iftype	/* the iftype we're managing here */
	,	&GenIfOps
	,	CloseGenInterfaceManager
	,	&GenIf
	,	(void**)&GenIfImports
	,	NULL);

	/* We don't ever want to be unloaded... */
	GenIfImports->ModRefCount(GenIf, +100);

	if (rc != PIL_OK) {
		GenPIImports->log(PIL_CRIT
		,	"Generic interface manager %s: unable to register"
		" to manage interface type %s: %s"
		,	PIL_PLUGIN_S, req->iftype
		,	PIL_strerror(rc));
	}
	return rc;
}

static void
CloseGeneralPluginManager(PILPlugin* us)
{
	int	count;
	/*
	 * All our clients have already been shut down automatically
	 * This is the final shutdown for us...
	 */

	/* There *shouldn't* be any keys in there ;-) */

	if ((count=g_hash_table_size(MasterTable)) > 0) {

		/* But just in case there are... */
		g_hash_table_foreach_remove(MasterTable, FreeAKey, NULL);
	}
	g_hash_table_destroy(MasterTable);
	MasterTable = NULL;
	return;
}

/*
 *	We get called for every time an implementation registers itself as
 *	implementing one of the kinds of interfaces we manage.
 *
 *	It's our job to make the implementation that's
 *	registering with us available to the system.
 *
 *	We do that by adding it to a GHashTable for its interface type
 *	Our users in the rest of the system takes it from there...
 *
 *	The key to the GHashTable is the implementation name, and the data is
 *	a pointer to the information the implementation exports.
 *
 *	It's a piece of cake ;-)
 */
static PIL_rc
RegisterGenIF(PILInterface* intf,  void** imports)
{
	PILGenericIfMgmtRqst*	ifinfo;

	/* Reference count should now be one */
	if (GenDebugFlag) {
		GenPIImports->log(PIL_DEBUG
		,	"%s IF manager: interface %s/%s registering."
		,	PIL_PLUGIN_S, intf->interfacetype->typename
		,	intf->interfacename);
	}
	g_assert(intf->refcnt == 1);
	/*
	 * We need to add it to the table that goes with this particular
	 * type of interface.
	 */
	if ((ifinfo = g_hash_table_lookup(MasterTable
	,	intf->interfacetype->typename)) !=	NULL)	{
		GHashTable*		ifmap = *(ifinfo->ifmap);

		g_hash_table_insert(ifmap, intf->interfacename,intf->exports);

		if (ifinfo->callback != NULL) {
			PILInterfaceType*	t = intf->interfacetype;

			if (GenDebugFlag) {
				GenPIImports->log(PIL_DEBUG
				,	"%s IF manager: callback 0x%lx"
				,	PIL_PLUGIN_S
				,	ifinfo->callback);
			}
			ifinfo->callback(PIL_REGISTER
			,	t->universe->piuniv, intf->interfacename
			,	t->typename, ifinfo->userptr);
		}

		*imports = ifinfo->importfuns;

		return PIL_OK;

	}else{
		GenPIImports->log(PIL_WARN
		,	"RegisterGenIF: interface type %s not found"
		,	intf->interfacename);
	}
	return PIL_INVAL;
}

/* Unregister an implementation -
 * 	We get called from the interface management system when someone
 * 	has requested that an implementation of a client interface be
 * 	unregistered.
 */
static PIL_rc
UnregisterGenIF(PILInterface*intf)
{
	PILGenericIfMgmtRqst*	ifinfo;

	g_assert(intf->refcnt >= 0);
	/*
	 * Go through the "master table" and find client table, 
	 * notify client we're about to remove this entry, then
	 * then remove this entry from it.
	 */
	if (GenDebugFlag) {
		GenPIImports->log(PIL_DEBUG
		,	"%s IF manager: unregistering interface %s/%s."
		,	PIL_PLUGIN_S, intf->interfacetype->typename
		,	intf->interfacename);
	}
	if ((ifinfo = g_hash_table_lookup(MasterTable
	,	intf->interfacetype->typename)) != NULL)	{

		GHashTable*		ifmap = *(ifinfo->ifmap);

		if (ifinfo->callback != NULL) {
			PILInterfaceType*	t = intf->interfacetype;
			if (GenDebugFlag) {
				GenPIImports->log(PIL_DEBUG
				,	"%s IF manager: callback 0x%lx"
				,	PIL_PLUGIN_S
				,	ifinfo->callback);
			}
			ifinfo->callback(PIL_UNREGISTER
			,	t->universe->piuniv, intf->interfacename
			,	t->typename, ifinfo->userptr);
		}

		/* Remove the client entry from master table */
		g_hash_table_remove(ifmap, intf->interfacename);

	}else{
		GenPIImports->log(PIL_WARN
		,	"UnregisterGenIF: interface type %s not found"
		,	intf->interfacename);
		return PIL_INVAL;
	}
	return PIL_OK;
}

/*
 *	Close down the generic interface manager.
 */
static PIL_rc
CloseGenInterfaceManager(PILInterface*intf, void* info)
{
	void*	key;
	void*	data;

	if (g_hash_table_lookup_extended(MasterTable
	,	intf->interfacetype->typename, &key, &data)) {
		g_hash_table_remove(MasterTable, key);
		g_hash_table_destroy((GHashTable*)data);
		g_free(key);
	}else{
		g_assert_not_reached();
	}
	return PIL_OK;
}

static gboolean
FreeAKey(gpointer key, gpointer value, gpointer data)
{
	g_free(key);
	return TRUE;
}
