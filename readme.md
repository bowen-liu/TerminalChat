# Terminal Chat
Terminal Chat is a multiuser, multichannel chat and file sharing program built on a multiple clients, single server model. 

## Prerequisites
TerminalChat is designed to run on Linux only, but porting it to other OS should not be very difficult. Both server and client has been tested on Ubuntu natively, as well as Ubuntu running on WSL. 

To compile the server and client executables, simply run the included MAKEFILE:

```make -f MAKEFILE```

Both server and client requires the readline() function to read from stdin. Install ```libreadline-dev``` if the library is not already installed.


## Getting Started
To run the server, the following arguments can be specified:

```./chatserver <ip>:<port>```

The _ip_ and _port_ fields specify which IP address and Port the server should bind a socket for listening, but are not required. If no _ip_ address is specified, INADDR_ANY will be used. If no _port_ is specified, the default port of 16996 will be used.

To run the client, the following arguments can be specified:

```./chatclient <desired_username> <server_ip>:<server_port>```

The _desired_username_ field is mandatory, and the client will automatically register the name specified with the server. Only alphanumeric characters, '.', '_', and '-' are permitted in usernames. If another with your name exists, your name will be appended with a number at the end.

The _server_ip_ field is mantdatory, but _server_port_ field isnt'. If _server_port_ isn't specified, the client will attempt to connect to the default port of 16996 of _server_ip_.


## Basic Usage

Upon succesfully connecting to the server and registering your username, you will automatically be placed into the lobby group called "all". Any regular messages sent without a target are directed to the lobby. You may leave the lobby at anytime with the command ```!leave @@all```.

To send a private message (or a command) to another user on the server, the message usually takes the form of:

```@<target_user> <msg_body>```

Simiarly, messages/commands targetted to a group will take the form of:

```@@<target_group> <msg_body>```

Note that target names starting with with a prefix of "@" will be interpreted as a user's name, whereas having a prefix of "@@" is interpreted as a group's name. 

If the first character of the message body is a "!", the message body will be interpreted as a command. Otherwise, the message body will simply be forwarded to the target as a regular chat message.

In the sections below, all available commands available will be documented.

## Group Commands

### Basic Group Operations
#### !newgroup
Syntax: ```!newgroup <group_name> <invited_user_1> <invited_user_2> ... <invited_user_n>```

The !newgroup command allows the caller to create a new group with the name specified as _group_name_. All groups must have unique names, or else the operation is rejected. Upon successfully creating a new group, the caller is automatically added to the group as an admin. 

The command also optionally allow the caller to specifiy a number of other users (as _invited_user_n_) to be invited automatically to the group after creation, all as group admins. 

### !userlist
Syntax: ```@@<group_name> !userlist```

The userlist command allows the client to obtain a list of all users current joined the group group_name. The calling client itself must have already joined the specified group_name in order to obtain the userlist. If a target group was not specified, a list of all users connected to the server is returned instead.

#### !join, !leave
Syntax: ``` !join <group_name> ```

An user may join an existing group (with the name _group_name_) with the !join command. If the target group's invite only flag is set, or the user has been previously banned from the target group, this command will be rejected.

Syntax: ``` !leave <group_name> ```

Conversely, an user may leave one of the groups he/she has previously joined (with the name _group_name_) using the !leave command. Once left, the user will no longer receive messages from the group he/she has left.

#### !invite
Syntax: ```@@<group_name> !invite <invited_user_1> <invited_user_2> ... <invited_user_n>```

An existing group member may add one or more new users (as _invited_user_n_) to a group (as _group_name_) by using the !invite command. All invited members are added as regular members, not admins. If the target group has the invite only flag set, this is the only way for new users to join the group.

The calling member must have the "CAN_INVITE" permission in group "_group_name_" to use the !invite command.


### Group Administrative Operations
#### !kick, !ban, !unban
Syntax: ```@@<group_name> !kick <member_1> <member_2> ... <member_n>```

If one or more members in a group (named _group_name_) is being disruptive, these members can be temporarily removed from a group by the !kick command. The kicked member can rejoin the group immediately after (or being invited back).

Syntax: ```@@<group_name> !ban <member_1> <member_2> ... <member_n>```
To permanently remove disruptive members from a group (named _group_name_), one or more members (named _member_n_) can be IP banned from the group by using the !ban command. No furthur users with a banned IP address can join the group again (even if the member was invited).

Syntax: ```@@<group_name> !unban <target_1> <target_2> ... <target_n>```
To revoke an IP ban in a group (named _group_name_), the !unban command is used. If a banned member is currently connected to the server, the IP ban for the member can be revoked by simply specifying _target_n_ as the member's username. 

If a banned member is no longer connected to the server, the only way to unban the member is by explicitly entering the IP address or hostname of the banned member as _target_n_ (if known). If the banned member's IP/hostname is not explicitly known, you must wait for the banned user to cone online again and unban its IP by using the member's username.

Note: The calling member must have the "CAN_KICK" permission in group "_group_name_" to use the !kick, !ban, and !unban command.

#### !setperm
Syntax: ```@@<group> !setperm <target> <permission_1> <permission_2> ... <permission_n>```

The !setperm command can be used to change a target's abilities within a _group_. The _target_ can be one of the following:
    -The username of a specific member
    -"all", applying to all members that has currently joined the group
    -"default", applying to all new members that will join the group from onwards

One or more permission altering operations (as _permission_n_) can be specified after _target_. These operations are applied from left to right, and a later permission may override an earlier applied permission. 

Below is a list of available operations that adds/removes an ability from the _target_ in _group_:

    -CAN_TALK   / CANNOT_TALK       : Ability to send messages. 
    -CAN_INVITE / CAN_INVITE        : Ability to invite new members. 
    -CAN_PUTFILE / CANNOT_PUTFILE   : Ability to upload files to share with other members
    -CAN_GETFILE / CANNOT_GETFILE   : Ability to download files uploaded by other members
    -CAN_KICK / CANNOT_KICK         : Ability to kick, ban, and unban another member
    -CAN_SETPERM / CANNOT_SETPERM   : Ability to change the permission of the group and other members

    -SET_USER                       : Reset a member's abilities to the group's "default" user permissions.
    -SET_ADMIN                      : Same as SET_USER, but with CAN_KICK and CAN_SETPERM.

In order to use the !setperm command, the caller must  have the CAN_SETPERM permission in the target group.

#### !setflag
Syntax: ```@@<group> !setflag <permission_1> <permission_2> ... <permission_n>```

The !setflag command changes settings for a targeted _group_, and will affect all joined users in the group.

One or more permission altering operations can be specified (as _permission_n_). These operations are applied from left to right, and a later permission may override an earlier applied permission. Below is a list of available operations that adds/removes flags from the target _group_:

    -SET_INVITE_ONLY / UNSET_INVITE_ONLY
        If a group's "INVITE_ONLY" flag is set, other users cannot freely join the group with the "!join" command. They must be invited by existing group members.

    -SET_TRANSFER_ALLOWED / UNSET_TRANSFER_ALLOWED
        If a group's "TRANSFER_ALLOWED" flag is set, group members are allowed to upload/download files to/from the group. This flag will override the user permission "CAN_PUTFILE" and "CAN_GETFILE".
    
    -SET_PERSISTENT / UNSET_PERSISTENT
        If a group's "PERSISTENT" flag is set, the group will not be deleted if all members leave. **This flag can only be changed by SERVER ADMINS.**

To use the !setflag command, the caller must have the CAN_SETPERM permission in the target group. Additionally, flags in the lobby group cannot be altered, even if the caller is a server admin.


## File Sharing Commands

### User-to-User File Transfer 
#### !sendfile
Syntax: ```@<user> !sendfile <filepath>```

The !sendfile command allows the caller to send an outgoing file (as _filepath_) directly to another _user_ connected to the server. The two users do not need to be in any same groups together. 

If the file specified in the _filepath_ is found, the client program will read the file's size and calculates its CRC before transmitting a file transfer invitation to the target _user_.

Note that an user can only have **one** ongoing or pending file transfer. If you send a !senfile request to another user before the first one was accepted, the first invitation will be cancelled. If the target user does not respond to the invitation after a fixed amount of time, the transfer invitation is automatically cancelled. 

#### !acceptfile
Syntax: ```@<user> !acceptfile```

The !acceptfile command lets you download an incoming file another _user_ that has previously offered to sent you.  

A user may receive multiple file sending invitations, but only one from each user. The client program keeps track of the pending files detailed offered by each user, therefore you only need to specify which user to accept an incoming file from. 


#### !rejectfile
Syntax: ```@<user> !rejectfile```

If you do not wish to receive an incoming file offered by another _user_, you may reject it with the !rejectfile command. Alternatively, you may also ignore the invitation and let it timeout.


#### !cancelfile
Syntax: ```!cancelfile```

The !cancelfile command has two uses:

    -The **sender** can use this command to cancel a pending outgoing file transfer invitation (!sendfile). Once the pending invitation has been cancelled, the target user can no longer accept it. 

    -Either **sender or receiver** can use this command to cancel an ongoing transfer.

Because each user can only have one pending or ongoing transfer, this command do not need a target to be specified.


### Group File Transfers

#### !putfile
Syntax: ```@@<group> !putfile <filepath>```

The !putfile command allows a group member to upload a file to a specified _group_. If the file specified in the _filepath_ is found, the client program will read the file's size and calculates its CRC, and transfer will commence with the server immediately. No third party is needed to accept the file. 

Upon successfully uploading the file, an announcement will be made about a new file available for download. This file will be downloadable by all group members (with !getfile) until the group is deleted, or the file is removed by the original uploader (with !removefile), or removed by a group admin. 

The same rule of only allowing one pending/ongoing file transfer applies to group transfers. You may also choose to cancel the ongoing group transfer by using the "!cancelfile" command.

The caller of this command must have the permission "CAN_PUTFILE" in the target _group_, and the target _group_ must have the "TRANSFER_ALLOWED" flag set.

#### !filelist
Syntax: ```@@<group> !filelist```

The !filelist command returns a list of uploaded files available to be downloaded in a specific _group_. The list will contain each file's **fileid** (needed for downloading the file), file name, file size, and uploader's name.

The caller of this command must have the permission "CAN_GETFILE" in the target _group_, and the target _group_ must have the "TRANSFER_ALLOWED" flag set.

#### !getfile
Syntax: ```@@<group> !getfile <fileid>```

The !getfile command allows a group member to download a file (with the associated _fileid_) from a target _group_. The associated _fileid_ for a file can be found using the !filelist command. If the fileid is valid for an uploaded file in the target _group_, file transfer will commence immediately with the server.

The same rule of only allowing one pending/ongoing file transfer applies to group transfers. You may also choose to cancel the ongoing group transfer by using the "!cancelfile" command.

The caller of this command must have the permission "CAN_GETFILE" in the target _group_, and the target _group_ must have the "TRANSFER_ALLOWED" flag set.

#### !removefile
Syntax: ```@@<group> !removefile <fileid>```

The !removefile command allows a previously uploaded file (with the associated _fileid_) to be removed from the target _group_. 

The caller of this command must have the same username as the uploader for the associated file in the target _group_. Alternatively, group administrators in the target _group_ can also use this command if they have the "CAN_KICK" permission.


## Server Administration Commands

These server administration commands can be entered directly through the server program's stdin/console. If remote admins wish to access these commands off-site, an "!admin" prefix is needed in front of the desired administration command (_admin_cmd_) in the following form:

Syntax: ```!admin <admin_cmd>```

By default, no remote connected users are server admins. An regular user can be promoted to a server administrator using the "!promoteuser" command, and demoted with the "!demoteuser" command.

In addition to having access to these server administration commands, server admins are also able to join any groups in the server (regardless of their invite only flags), and are automatically made group admins upon joining a group. Server admins have an immense amount of power, and must be used responsibly. Choose your server administrators with care. 

#### !shutdown
Syntax: ```!shutdown```

The !shutdown command will terminate the server, and drop all clients and connections.


#### !bcast
Syntax: ```!bcast <long_msg>```

The !bcast command directly sends a message (_long_msg_) to all users connected to the server, regardless of what groups they're in. The message will show up on each client's screen as you've entered, without any additional prefixes or headers.

The specified message (_long_msg_) bypasses the default maximum message length, and will be sent to clients with partial queuing if needed.

#### !delgroup
Syntax: ```!delgroup <group>```

Server administrators can arbritrarily delete any _group_ (except for lobby and other persistent groups) at any time. All group members will be gracefully removed from the _group_ with a messaging telling them the group has been closed. All uploaded files are also deleted.


#### !dropuser
Syntax: ```!dropuser <user>```

If an _user_ is being disruptive to multiple users, multiple groups, or to the server in general, server admins can gracefully disconnect the disruptive _user_ with the !dropuser command. The disconnected _user_ can reconnect back to the server at any time.

When a user has been dropped, the user will be removed from all participating groups. Each involved group is then notified with a message saying the user has been dropped by a server admin. 

#### !banip
Syntax: ```!banip <target>```

If an user is being heavily disruptive, the user can be permanently removed from the server with the !banip command. The server administrator can specify _target_ as an username (and its associated IP address will be banned), or specify an IP address/hostname to be banned directly. 

When an user has been IP banned, all connected users sharing the same IP are immediately dropped. No further connections to the server are permitted on the banned IP address. 

#### !unbanip
Syntax: ```!unbanip <target>```

A previously banned IP address or hostname (as _target_) can be lifted by using the !unbanip command. 

Unlike the !banip command, a banned user's username cannot be specified as _target_, as the server does not keep track of banned usernames at this time. 

#### !promoteuser
Syntax: ```!promoteuser <user>```

The !promoteuser command grants server administrative abilities to a target _user_. Use this command responsibly!

#### !demoteuser
Syntax: ```!promoteuser <user>```

The !demoteuser command removes server administrative abilities from a target _user_.
