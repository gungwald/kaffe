
/*
 * Java core library component.
 *
 * Copyright (c) 1997, 1998
 *      Transvirtual Technologies, Inc.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file.
 */

package java.util;

import java.io.File;
import java.io.FilenameFilter;
import java.io.IOException;
import java.io.Serializable;
import kaffe.util.UNIXTimeZone;

abstract public class TimeZone implements Serializable, Cloneable {
	private static final long serialVersionUID = 3581463369166924961L;
	private static TimeZone defaultTimeZone = null;
	private static HashMap zones = new HashMap();

	private static String[] zoneDirs = {
		"/usr/share/zoneinfo",
		"/usr/share/lib/zoneinfo",
		// XXX others to try??
	};

	private String timezoneID = null;

// Load the system timezones
static {

	// Install some standard SimpleTimeZones
	addSimple(-11*60*60*1000, "MIT");
	addSimple(-10*60*60*1000, "HST");
	addSimple(-9*60*60*1000, "AST");
	addSimple(-8*60*60*1000, "PST", Calendar.APRIL, 1, Calendar.SUNDAY, 2*60*60*1000, Calendar.OCTOBER, -1, Calendar.SUNDAY, 2*60*60*1000);
	addSimple(-8*60*60*1000, "PDT", Calendar.APRIL, 1, Calendar.SUNDAY, 2*60*60*1000, Calendar.OCTOBER, -1, Calendar.SUNDAY, 2*60*60*1000);
	addSimple(-7*60*60*1000, "PNT");
	addSimple(-7*60*60*1000, "MST", Calendar.APRIL, 1, Calendar.SUNDAY, 2*60*60*1000, Calendar.OCTOBER, -1, Calendar.SUNDAY, 2*60*60*1000);
	addSimple(-7*60*60*1000, "MDT", Calendar.APRIL, 1, Calendar.SUNDAY, 2*60*60*1000, Calendar.OCTOBER, -1, Calendar.SUNDAY, 2*60*60*1000);
	addSimple(-6*60*60*1000, "CST", Calendar.APRIL, 1, Calendar.SUNDAY, 2*60*60*1000, Calendar.OCTOBER, -1, Calendar.SUNDAY, 2*60*60*1000);
	addSimple(-6*60*60*1000, "CDT", Calendar.APRIL, 1, Calendar.SUNDAY, 2*60*60*1000, Calendar.OCTOBER, -1, Calendar.SUNDAY, 2*60*60*1000);
	addSimple(-5*60*60*1000, "EST", Calendar.APRIL, 1, Calendar.SUNDAY, 2*60*60*1000, Calendar.OCTOBER, -1, Calendar.SUNDAY, 2*60*60*1000);
	addSimple(-5*60*60*1000, "EDT", Calendar.APRIL, 1, Calendar.SUNDAY, 2*60*60*1000, Calendar.OCTOBER, -1, Calendar.SUNDAY, 2*60*60*1000);
	addSimple(-5*60*60*1000, "IET");
	addSimple(-4*60*60*1000, "PRT");
	addSimple(-3500*60*60, "CNT");
	addSimple(-3*60*60*1000, "AGT");
	addSimple(-1*60*60*1000, "CAT");
	addSimple(0, "GMT");
	addSimple(1*60*60*1000, "ECT");
	addSimple(1*60*60*1000, "EET");
	addSimple(2*60*60*1000, "ART");
	addSimple(3*60*60*1000, "EAT");
	addSimple(3500*60*60, "MET");
	addSimple(4*60*60*1000, "NET");
	addSimple(5*60*60*1000, "PLT");
	addSimple(5500*60*60, "IST");
	addSimple(6*60*60*1000, "BST");
	addSimple(7*60*60*1000, "VST");
	addSimple(8*60*60*1000, "CTT");
	addSimple(9*60*60*1000, "JST");
	addSimple(9500*60*60, "ACT");
	addSimple(10*60*60*1000, "AET");
	addSimple(11*60*60*1000, "SST");
	addSimple(12*60*60*1000, "NST");

	// Now read in any 'TZif' timezone files we find
	for (int i = 0; i < zoneDirs.length; i++) {
		File dir = new File(zoneDirs[i]);
		if (dir.isDirectory())
			addZoneFiles(null, dir);
	}
}

private static void addSimple(int rawOffset, String id) {
	zones.put(id, new SimpleTimeZone(rawOffset, id));
}

private static void addSimple(int off, String id, int i1,
		int i2, int i3, int i4, int i5, int i6, int i7, int i8) {
	zones.put(id, new SimpleTimeZone(off, id, i1, i2, i3, i4,
		i5, i6, i7, i8));
}

// Recurse through a directory tree adding any UNIX zone files found
private static void addZoneFiles(String prefix, File dir) {

	// Get directory listing of normal files and directories
	String[] files = dir.list(new FilenameFilter() {
		public boolean accept(File dir0, String name) {
			return new File(dir0, name).isFile();
		}
	});
	String[] dirs = dir.list(new FilenameFilter() {
		public boolean accept(File dir0, String name) {
			return new File(dir0, name).isDirectory();
		}
	});

	// Add zone files
	for (int i = 0; i < files.length; i++) {
		try {
			zones.put(prefix == null ?
			    files[i] : prefix + "/" + files[i],
			    new UNIXTimeZone(new File(dir, files[i])));
		} catch (IOException e) {
		}
	}

	// Recurse
	for (int i = 0; i < dirs.length; i++) {
		addZoneFiles(prefix == null ?
		    dirs[i] : prefix + "/" + dirs[i],
		    new File(dir, dirs[i]));
	}
}

public TimeZone() {
}

public Object clone() {
	try {
		return (TimeZone)super.clone();
	} catch (Exception e) {
		return null;
	}
}

public static synchronized String[] getAvailableIDs() {
	return (String[])zones.keySet().toArray(new String[zones.size()]);
}

public static synchronized String[] getAvailableIDs(int rawOffset) {
	HashSet ids = new HashSet();
	for (Iterator i = zones.entrySet().iterator(); i.hasNext(); ) {
		Map.Entry ent = (Map.Entry)i.next();
		TimeZone tz = (TimeZone)ent.getValue();
		if (tz.getRawOffset() == rawOffset)
			ids.add(ent.getKey());
	}
	return (String[])ids.toArray(new String[ids.size()]);
}

public static synchronized TimeZone getDefault()
{
	if (defaultTimeZone == null) {
		String zne = System.getProperty("user.timezone", "GMT");
		defaultTimeZone = getTimeZone(zne);
		if (defaultTimeZone == null) {
			defaultTimeZone = getTimeZone("GMT");
		}
		if (defaultTimeZone == null) {
			throw new InternalError("Cannot intialize timezone."
			    + " GMT & " +zne+ " zones are undefined.");
		}
	}
	return (defaultTimeZone);
}

public String getID()
{
	return (timezoneID);
}

abstract public int getOffset(int era, int year, int month, int day, int dayOfWeek, int milliseconds);

abstract public int getRawOffset();

public static synchronized TimeZone getTimeZone(String ID) {
	return (TimeZone)zones.get(ID);
}

abstract public boolean inDaylightTime(Date date);

public static synchronized void setDefault(TimeZone zone)
{
	defaultTimeZone = zone;
}

public void setID(String ID)
{
	timezoneID = ID;
}

abstract public void setRawOffset(int offsetMillis);

public String toString()
{
	return (timezoneID);
}

abstract public boolean useDaylightTime();
}
