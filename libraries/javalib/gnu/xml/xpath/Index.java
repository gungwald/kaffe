/*
 * Index.java
 * Copyright (C) 2004 The Free Software Foundation
 * 
 * This file is part of GNU JAXP, a library.
 *
 * GNU JAXP is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * GNU JAXP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Linking this library statically or dynamically with other modules is
 * making a combined work based on this library.  Thus, the terms and
 * conditions of the GNU General Public License cover the whole
 * combination.
 *
 * As a special exception, the copyright holders of this library give you
 * permission to link this library with independent modules to produce an
 * executable, regardless of the license terms of these independent
 * modules, and to copy and distribute the resulting executable under
 * terms of your choice, provided that you also meet, for each linked
 * independent module, the terms and conditions of the license of that
 * module.  An independent module is a module which is not derived from
 * or based on this library.  If you modify this library, you may extend
 * this exception to your version of the library, but you are not
 * obliged to do so.  If you do not wish to do so, delete this
 * exception statement from your version. 
 */

package gnu.xml.xpath;

import java.util.Collection;
import java.util.Collections;
import org.w3c.dom.Node;

/**
 * Returns the specified numbered child of the context node.
 *
 * @author <a href='mailto:dog@gnu.org'>Chris Burdess</a>
 */
class Index
extends Expr
{

  final Expr lhs;
  final Expr rhs;

  Index (Expr lhs, Expr rhs)
  {
    this.lhs = lhs;
    this.rhs = rhs;
  }

  public Object evaluate (Node context)
  {
    Object left = lhs.evaluate (context);
    if (left instanceof Collection)
      {
        Object right = rhs.evaluate (context);
        double n = _number (context, right);
        int index = ((int) n) - 1;
        
        Collection ns = (Collection) left;
        Node[] nodes = new Node[ns.size ()];
        ns.toArray (nodes);
        if (index >= 0 && index < nodes.length)
          {
            return Collections.singleton (nodes[index]);
          }
      }
    return null;
  }

  public String toString ()
  {
    return lhs.toString() + '[' + rhs.toString() + ']';
  }
  
}