//using Microsoft.VisualBasic.CompilerServices;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Threading;

namespace EvalAdisApi
{
	public class AdisObject
	{
		//
		// Properties
		//
		public string FirmwareInfo
		{
			get
			{
				return this.fCore.FirmwareInfo ();
			}
		}

		public object SoftwareInfo
		{
			get
			{
				return "1.0.1.3098";
			}
		}

		public int StreamProgress
		{
			get
			{
				return Conversions.ToInteger (this.fCore.StreamPercentProgress);
			}
		}

		public bool StreamRunning
		{
			get
			{
				return Conversions.ToBoolean (this.fCore.StreamRunning);
			}
		}

		public string StreamRunTimeErrorMsg
		{
			get
			{
				return Conversions.ToString (this.fCore.StreamRunTimeErrorMsg);
			}
		}

		//
		// Constructors
		//
		public AdisObject ()
		{
			this.sTrap = new AdisObject.StartTrap ();
			this.fCore = new AdisObject.CoreFunctions ();
		}

		//
		// Methods
		//
		public void CancelStream ()
		{
			this.fCore.CancelStream ();
		}

		public bool Connect ()
		{
			return this.fCore.Connect ();
		}

		public void Disconnect ()
		{
			this.fCore.Disconnect ();
		}

		public double GetDataTransferTimeInMilliSec (IList<uint> addr)
		{
			return this.fCore.GetDataTransferTimeInMilliSec (addr);
		}

		public double GetDrFreqInHz ()
		{
			return this.fCore.GetDrFreqInHz ();
		}

		public ushort[] ReadRegArray (IList<uint> addr, uint numCaptures, bool drActive)
		{
			return this.fCore.ReadRegArray (addr, numCaptures, drActive);
		}

		public ushort ReadRegByte (uint addr)
		{
			return this.fCore.ReadRegByte (addr);
		}

		public ushort ReadRegWord (uint addr)
		{
			return this.fCore.ReadRegWord (addr);
		}

		public object ReadStreamQueue ()
		{
			return this.fCore.ReadStreamQueue ();
		}

		public void StartStream (IList<uint> addr, double numSamples, bool drActive)
		{
			this.fCore.StartStream (addr, numSamples, drActive);
		}

		public void WriteRegByte (uint addr, uint data)
		{
			this.fCore.WriteRegByte (addr, data);
		}

		public void WriteRegWord (uint addr, uint data)
		{
			this.fCore.WriteRegWord (addr, data);
		}

		//
		// Nested Types
		//
		private class CoreFunctions
		{
			private class StreamArgs
			{
				public IList<uint> _addr;

				public uint _numCaptures;

				public uint _numBuffers;

				[DebuggerNonUserCode]
				public StreamArgs ()
				{
				}
			}

			private virtual BackgroundWorker bgWorker
			{
				[DebuggerNonUserCode]
				get
				{
					return this._bgWorker;
				}
				[DebuggerNonUserCode]
				[MethodImpl (32)]
				set
				{
					ProgressChangedEventHandler value2 = new ProgressChangedEventHandler (this.backGroundWorker_ProgressChanged);
					RunWorkerCompletedEventHandler value3 = new RunWorkerCompletedEventHandler (this.backGroundWorker_RunWorkCompleted);
					DoWorkEventHandler value4 = new DoWorkEventHandler (this.backGroundWorker_DoWork);
					if (this._bgWorker != null)
					{
						this._bgWorker.ProgressChanged -= value2;
						this._bgWorker.RunWorkerCompleted -= value3;
						this._bgWorker.DoWork -= value4;
					}
					this._bgWorker = value;
					if (this._bgWorker != null)
					{
						this._bgWorker.ProgressChanged += value2;
						this._bgWorker.RunWorkerCompleted += value3;
						this._bgWorker.DoWork += value4;
					}
				}
			}

			internal object StreamRunTimeErrorMsg
			{
				get
				{
					return this.ErrorStreamMsg;
				}
			}

			internal object StreamRunning
			{
				get
				{
					return this.bgWorker.IsBusy | this.bgWorkAlwaysTrueExceptDone;
				}
			}

			internal object StreamPercentProgress
			{
				get
				{
					object obj = AdisObject.CoreFunctions.objStreamProgressLock;
					ObjectFlowControl.CheckForSyncLockOnValueType (obj);
					Monitor.Enter (obj);
					object streamPercentProgress;
					try
					{
						streamPercentProgress = AdisObject.CoreFunctions._StreamPercentProgress;
					}
					finally
					{
						Monitor.Exit (obj);
					}
					return streamPercentProgress;
				}
				set
				{
					object obj = AdisObject.CoreFunctions.objStreamProgressLock;
					ObjectFlowControl.CheckForSyncLockOnValueType (obj);
					Monitor.Enter (obj);
					try
					{
						AdisObject.CoreFunctions._StreamPercentProgress = RuntimeHelpers.GetObjectValue (value);
					}
					finally
					{
						Monitor.Exit (obj);
					}
				}
			}
		}

		private class StartTrap
		{
			public StartTrap ()
			{
				AppDomain.get_CurrentDomain ().add_AssemblyResolve (new ResolveEventHandler (this.AssemblyResolver));
			}
		}
	}
}

